//
// (C) Copyright 2011-2012 Sergey A. Babkin.
// This file is a part of Triceps.
// See the file COPYRIGHT for the copyright notice and license information
//
//
// The table implementation.

#include <table/Table.h>
#include <type/TableType.h>
#include <type/AggregatorType.h>
#include <type/RootIndexType.h>
#include <sched/AggregatorGadget.h>
#include <mem/Rhref.h>
#include <common/Exception.h>
#include <common/BusyMark.h>

namespace TRICEPS_NS {

////////////////////////////////////// Table::InputLabel ////////////////////////////////////

Table::InputLabel::InputLabel(Unit *unit, const_Onceref<RowType> rtype, const string &name, Table *table) :
	Label(unit, rtype, name),
	table_(table)
{ }

void Table::InputLabel::execute(Rowop *arg) const
{
	if (arg->isInsert()) {
		table_->insertRow(arg->getRow()); // ignore the failures
	} else if (arg->isDelete()) {
		table_->deleteRow(arg->getRow());
	}
}

////////////////////////////////////// Table ////////////////////////////////////

Table::Table(Unit *unit, EnqMode emode, const string &name, 
	const TableType *tt, const RowType *rowt, const RowHandleType *handt) :
	Gadget(unit, emode, name + ".out", rowt),
	type_(tt),
	rowType_(rowt),
	rhType_(handt),
	inputLabel_(new InputLabel(unit, rowt, name + ".in", this)),
	firstLeaf_(tt->getFirstLeaf()),
	preLabel_(new DummyLabel(unit, rowt, name + ".pre")),
	name_(name),
	busy_(false)
{ 
	root_ = static_cast<RootIndex *>(tt->root_->makeIndex(tt, this));
	// fprintf(stderr, "DEBUG Table::Table root=%p\n", root_.get());

	// create gadgets for all the aggregators
	size_t n = tt->aggs_.size();
	for (size_t i = 0; i < n; i++) {
		aggs_.push_back(tt->aggs_[i].agg_->makeGadget(this, tt->aggs_[i].index_));
	}
}

Table::~Table()
{
	// fprintf(stderr, "DEBUG Table::~Table root=%p\n", root_.get());

	// remove all the rows in the table: this goes more efficiently
	// if we first move them to a vector, clear the indexes and delete from vector;
	// otherwise the index rebalancing during deletion takes a much longer time
	vector <RowHandle *> rows;
	rows.reserve(root_->size());

	{
		RowHandle *rh;
		for (rh = root_->begin(); rh != NULL; rh = root_->next(rh))
			rows.push_back(rh);
	}

	root_->clearData();

	for (vector <RowHandle *>::iterator it = rows.begin(); it != rows.end(); ++it) {
		RowHandle *rh = *it;
		rh->flags_ &= ~RowHandle::F_INTABLE;
		if (rh->decref() <= 0)
			destroyRowHandle(rh);
	}
}

Label *Table::getAggregatorLabel(const string &agname) const
{
	// do a simple linear search
	for (AggGadgetVec::const_iterator it = aggs_.begin(); it != aggs_.end(); ++it) {
		if ( (*it)->getType()->getName() == agname) {
			return (*it)->getLabel();
		}
	}
	return NULL;
}

RowHandle *Table::makeRowHandle(const Row *row) const
{
	if (row == NULL)
		return NULL;

	row->incref();
	RowHandle *rh = rhType_->makeHandle(row);
	// for each index, fill in the cached key information
	type_->root_->initRowHandle(rh);

	return rh;
}

void Table::destroyRowHandle(RowHandle *rh) const
{
	// for each index, clear whatever per-handle internal objects there may be
	type_->root_->clearRowHandle(rh);
	Row *row = const_cast<Row *>(rh->row_);
	if (row->decref() <= 0)
		rowType_->destroyRow(row);
	delete rh;
}

bool Table::insertRow(const Row *row, Tray *copyTray)
{
	if (row == NULL)
		return false;

	RowHandle *rh = makeRowHandle(row);
	rh->incref();

	bool res;
	Erref err;
	try {
		res = insert(rh, copyTray);
	} catch (Exception e) {
		err = e.getErrors();
	}

	if (rh->decref() <= 0)
		destroyRowHandle(rh);

	if (!err.isNull())
		throw Exception(err, false);

	return res;
}

bool Table::insert(RowHandle *newrh, Tray *copyTray)
{
	if (newrh == NULL)
		return false;

	if (newrh->isInTable())
		return false;  // nothing to do

	if (busy_)
		throw Exception(strprintf("Detected a recursive modification of the table '%s'.", getName().c_str()), false);

	BusyMark bm(busy_); // will auto-clean on exit

	bool noAggs = aggs_.empty();
	Autoref<Tray> aggTray; // delayed records from aggregation
	if (!noAggs)
		aggTray = new Tray;

	Index::RhSet emptyRhSet; // always empty here
	Index::RhSet replace;
	Index::RhSet changed;
	vector<RowHandle *> deref; // row handles that need to be dereferenced

	try {
		if (!root_->replacementPolicy(newrh, replace)) {
			// this may have created the groups for the new record that didn't get inserted, so collapse them back
			changed.insert(newrh); // OK to add, since the iterators in newrh get populated by replacementPolicy()
			root_->collapse(aggTray, changed, copyTray); // aggTray may be NULL, it's OK with no aggregators
			// aggTray should be empty, so don't send it anywhere
			return false;
		}

		if (!noAggs) {
			changed.insert(newrh); // OK to add, since the iterators in newrh got populated by replacementPolicy()
			root_->aggregateBefore(aggTray, replace, emptyRhSet, copyTray);
			root_->aggregateBefore(aggTray, changed, replace, copyTray);
			// Aggregator "before" changes go before table changes. If there are multiple aggregators,
			// between themselves they go sort of in parallel.
			unit_->enqueueDelayedTray(aggTray); // may throw
			aggTray->clear();
		}

		// delete the rows that are pushed out but don't collapse the groups yet
		for (Index::RhSet::iterator rsit = replace.begin(); rsit != replace.end(); ++rsit) {
			RowHandle *rh = *rsit;
			if (preLabel_->hasChained()) {
				Autoref<Rowop> rop = new Rowop(preLabel_, Rowop::OP_DELETE, rh->getRow());
				unit_->call(rop); // may throw
			}
			root_->remove(rh);
			rh->flags_ &= ~RowHandle::F_INTABLE;
			deref.push_back(rh);
			send(rh->getRow(), Rowop::OP_DELETE, copyTray); // may throw
		}

		if (preLabel_->hasChained()) {
			Autoref<Rowop> rop = new Rowop(preLabel_, Rowop::OP_INSERT, newrh->getRow());
			unit_->call(rop); // may throw
		}
		
		// now keep the table-wide reference to that new handle
		newrh->incref();
		newrh->flags_ |= RowHandle::F_INTABLE;

		root_->insert(newrh);
		send(newrh->getRow(), Rowop::OP_INSERT, copyTray); // may throw

		if (!noAggs) {
			root_->aggregateAfter(aggTray, Aggregator::AO_AFTER_DELETE, replace, changed, copyTray);
			root_->aggregateAfter(aggTray, Aggregator::AO_AFTER_INSERT, changed, emptyRhSet, copyTray);
			// Aggregator "after" changes go after table changes. If there are multiople aggregators,
			// between themselves they go sort of in parallel.
			unit_->enqueueDelayedTray(aggTray); // may throw
			aggTray->clear();
		}

		// finally, collapse the groups of the replaced records
		root_->collapse(aggTray, replace, copyTray);

		if (!noAggs && !aggTray->empty()) {
			// The aggregators may have produced more output on collapse.
			unit_->enqueueDelayedTray(aggTray); // may throw
			aggTray->clear();
		}

		// and then the removed rows get unreferenced by the table
		for (vector<RowHandle *>::iterator rsit = deref.begin(); rsit != deref.end(); ++rsit) {
			RowHandle *rh = *rsit;
			if (rh->decref() <= 0)
				destroyRowHandle(rh);
		}
	} catch (Exception e) {
		// the removed rows must get unreferenced by the table
		for (vector<RowHandle *>::iterator rsit = deref.begin(); rsit != deref.end(); ++rsit) {
			RowHandle *rh = *rsit;
			if (rh->decref() <= 0)
				destroyRowHandle(rh);
		}
		// XXX this leaves the empty groups uncollapsed
		throw;
	}
	
	return true;
}

void Table::remove(RowHandle *rh, Tray *copyTray)
{
	if (rh == NULL || !rh->isInTable())
		return;

	if (busy_)
		throw Exception(strprintf("Detected a recursive modification of the table '%s'.", getName().c_str()), false);

	BusyMark bm(busy_); // will auto-clean on exit

	bool noAggs = aggs_.empty();
	Autoref<Tray> aggTray; // delayed records from aggregation
	if (!noAggs)
		aggTray = new Tray;

	Index::RhSet emptyRhSet; // always empty here
	Index::RhSet replace;
	replace.insert(rh);
	RowHandle *rhdec = NULL; // remember to decrease the reference after removing from the table

	try {
		if (!noAggs) {
			root_->aggregateBefore(aggTray, replace, emptyRhSet, copyTray);
			// Aggregator "before" changes go before table changes. If there are multiople aggregators,
			// between themselves they go sort of in parallel.
			unit_->enqueueDelayedTray(aggTray); // may throw
			aggTray->clear();
		}

		if (preLabel_->hasChained()) {
			Autoref<Rowop> rop = new Rowop(preLabel_, Rowop::OP_DELETE, rh->getRow());
			unit_->call(rop); // may throw
		}

		root_->remove(rh);
		rh->flags_ &= ~RowHandle::F_INTABLE;
		rhdec = rh;

		send(rh->getRow(), Rowop::OP_DELETE, copyTray); // may throw

		if (!noAggs) {
			root_->aggregateAfter(aggTray, Aggregator::AO_AFTER_DELETE, replace, emptyRhSet, copyTray);
			// Aggregator "after" changes go after table changes. If there are multiple aggregators,
			// between themselves they go sort of in parallel.
			unit_->enqueueDelayedTray(aggTray); // may throw
			aggTray->clear();
		}

		root_->collapse(aggTray, replace, copyTray);
		
		if (!noAggs && !aggTray->empty()) {
			// The aggregators may have produced more output on collapse.
			unit_->enqueueDelayedTray(aggTray); // may throw
			aggTray->clear();
		}

		if (rhdec->decref() <= 0)
			destroyRowHandle(rhdec);
	} catch (Exception e) {
		// the removed rows must get unreferenced by the table
		if (rhdec) {
			if (rhdec->decref() <= 0)
				destroyRowHandle(rhdec);
		}
		// XXX this leaves the empty groups uncollapsed
		throw;
	}
}

bool Table::deleteRow(const Row *row, Tray *copyTray)
{
	Rhref what(this, makeRowHandle(row));
	RowHandle *rh = find(what);
	if (rh != NULL) {
		remove(rh, copyTray); // may throw
		return true;
	}
	return false;
}

RowHandle *Table::begin() const
{
	return root_->begin();
}

RowHandle *Table::beginIdx(IndexType *ixt) const
{
	if (ixt == NULL || ixt->getTabtype() != type_)
		return NULL;

	return ixt->beginIterationIdx(this);
}

RowHandle *Table::next(const RowHandle *cur) const
{
	return root_->next(cur);
}

RowHandle *Table::nextIdx(IndexType *ixt, const RowHandle *cur) const
{
	if (ixt == NULL || ixt->getTabtype() != type_ || cur == NULL || !cur->isInTable())
		return NULL;

	return ixt->nextIterationIdx(this, cur);
}

RowHandle *Table::firstOfGroupIdx(IndexType *ixt, const RowHandle *cur) const
{
	if (ixt == NULL || ixt->getTabtype() != type_ || cur == NULL || !cur->isInTable())
		return NULL;

	return ixt->firstOfGroupIdx(this, cur);
}

RowHandle *Table::nextGroupIdx(IndexType *ixt, const RowHandle *cur) const
{
	if (ixt == NULL || ixt->getTabtype() != type_ || cur == NULL || !cur->isInTable())
		return NULL;

	return ixt->nextGroupIdx(this, cur);
}

RowHandle *Table::lastOfGroupIdx(IndexType *ixt, const RowHandle *cur) const
{
	if (ixt == NULL || ixt->getTabtype() != type_ || cur == NULL || !cur->isInTable())
		return NULL;

	return ixt->lastOfGroupIdx(this, cur);
}

RowHandle *Table::findIdx(IndexType *ixt, const RowHandle *what) const
{
	if (ixt == NULL || ixt->getTabtype() != type_)
		return NULL;

	return ixt->findRecord(this, what);
}

RowHandle *Table::findRowIdx(IndexType *ixt, const Row *row) const
{
	if (row == NULL)
		return NULL;

	RowHandle *rh = makeRowHandle(row);
	rh->incref();

	RowHandle *res = findIdx(ixt, rh);

	if (rh->decref() <= 0)
		destroyRowHandle(rh);

	return res;
}

}; // TRICEPS_NS

