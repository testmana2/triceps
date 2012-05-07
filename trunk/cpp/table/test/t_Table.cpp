//
// (C) Copyright 2011-2012 Sergey A. Babkin.
// This file is a part of Triceps.
// See the file COPYRIGHT for the copyright notice and license information
//
//
// Test of the table general functionality that doesn't depend on
// the indexes used (most of the table stuff does and is tested per 
// index type).

#include <utest/Utest.h>
#include <string.h>

#include <type/AllTypes.h>
#include <common/StringUtil.h>
#include <table/Table.h>
#include <mem/Rhref.h>

// Make fields of all simple types
void mkfields(RowType::FieldVec &fields)
{
	fields.clear();
	fields.push_back(RowType::Field("a", Type::r_uint8, 10));
	fields.push_back(RowType::Field("b", Type::r_int32,0));
	fields.push_back(RowType::Field("c", Type::r_int64));
	fields.push_back(RowType::Field("d", Type::r_float64));
	fields.push_back(RowType::Field("e", Type::r_string));
}

uint8_t v_uint8[10] = "123456789";
int32_t v_int32 = 1234;
int64_t v_int64 = 0xdeadbeefc00c;
double v_float64 = 9.99e99;
char v_string[] = "hello world";

void mkfdata(FdataVec &fd)
{
	fd.resize(4);
	fd[0].setPtr(true, &v_uint8, sizeof(v_uint8));
	fd[1].setPtr(true, &v_int32, sizeof(v_int32));
	fd[2].setPtr(true, &v_int64, sizeof(v_int64));
	fd[3].setPtr(true, &v_float64, sizeof(v_float64));
	// test the constructor
	fd.push_back(Fdata(true, &v_string, sizeof(v_string)));
}

// records the table size in the tracer's buffer when called
class LabelTraceSize : public Label
{
public:
	LabelTraceSize(Unit *unit, Onceref<RowType> rtype, const string &name,
			Table *table, Unit::StringTracer *tracer) :
		Label(unit, rtype, name),
		table_(table),
		tracer_(tracer)
	{ }

	virtual void execute(Rowop *arg) const
	{
		tracer_->getBuffer()->appendMsg(false, strprintf("table size %d", (int)table_->size()));
	}

	Table *table_;
	Unit::StringTracer *tracer_;
};

UTESTCASE preLabel(Utest *utest)
{
	RowType::FieldVec fld;
	mkfields(fld);

	Autoref<Unit> unit = new Unit("u");
	Autoref<Unit::StringNameTracer> trace = new Unit::StringNameTracer;
	unit->setTracer(trace);

	Autoref<RowType> rt1 = new CompactRowType(fld);
	UT_ASSERT(rt1->getErrors().isNull());

	Autoref<TableType> tt = (new TableType(rt1))
		->addSubIndex("primary", new HashedIndexType(
			(new NameSet())->add("b")->add("c"))
		);

	UT_ASSERT(tt);
	tt->initialize();
	UT_ASSERT(tt->getErrors().isNull());
	UT_ASSERT(!tt->getErrors()->hasError());

	Autoref<Table> t = tt->makeTable(unit, Table::EM_CALL, "t");
	UT_ASSERT(!t.isNull());
	UT_ASSERT(t->getInputLabel() != NULL);
	UT_ASSERT(t->getPreLabel() != NULL);
	UT_ASSERT(t->getLabel() != NULL);
	UT_IS(t->getInputLabel()->getName(), "t.in");
	UT_IS(t->getPreLabel()->getName(), "t.pre");
	UT_IS(t->getLabel()->getName(), "t.out");

	FdataVec dv;
	mkfdata(dv);
	
	int32_t one32 = 1;
	int64_t one64 = 1, two64 = 2;

	dv[1].data_ = (char *)&one32; dv[2].data_ = (char *)&one64;
	Rowref r11(rt1,  rt1->makeRow(dv));
	Rhref rh11(t, t->makeRowHandle(r11));

	dv[1].data_ = (char *)&one32; dv[2].data_ = (char *)&two64;
	Rowref r12(rt1,  rt1->makeRow(dv));
	Rhref rh12(t, t->makeRowHandle(r12));

	string tlog;
	
	// check that if nothing is chained from .pre, it's not called
	UT_ASSERT(t->insertRow(r11));
	tlog = trace->getBuffer()->print();
	UT_IS(tlog, "unit 'u' before label 't.out' op OP_INSERT\n");
	
	// check that if something is chained from .pre, it's called
	Autoref<Label> dummy = new DummyLabel(unit, rt1, "dummy");
	UT_ASSERT(t->getPreLabel()->chain(dummy).isNull());

	trace->clearBuffer();
	UT_ASSERT(t->insertRow(r12));
	tlog = trace->getBuffer()->print();
	UT_IS(tlog, 
		"unit 'u' before label 't.pre' op OP_INSERT\n"
		"unit 'u' before label 'dummy' (chain 't.pre') op OP_INSERT\n"
		"unit 'u' before label 't.out' op OP_INSERT\n"
	);

	// check the sequence of calls between .pre and .out
	trace->clearBuffer();
	UT_ASSERT(t->insertRow(r11)); // this will do a DELETE then INSERT
	tlog = trace->getBuffer()->print();
	UT_IS(tlog, 
		"unit 'u' before label 't.pre' op OP_DELETE\n"
		"unit 'u' before label 'dummy' (chain 't.pre') op OP_DELETE\n"
		"unit 'u' before label 't.out' op OP_DELETE\n"
		"unit 'u' before label 't.pre' op OP_INSERT\n"
		"unit 'u' before label 'dummy' (chain 't.pre') op OP_INSERT\n"
		"unit 'u' before label 't.out' op OP_INSERT\n"
	);

	// check that the table gets updated every time before
	// calling the output label
	Autoref<LabelTraceSize> lsize = new LabelTraceSize(unit, rt1, "lsize", t, trace);
	t->getPreLabel()->clearChained();
	UT_ASSERT(t->getPreLabel()->chain(lsize).isNull());
	UT_ASSERT(t->getLabel()->chain(lsize).isNull());

	trace->clearBuffer();
	UT_ASSERT(t->insertRow(r12)); // this will do a DELETE then INSERT
	tlog = trace->getBuffer()->print();
	UT_IS(tlog, 
		"unit 'u' before label 't.pre' op OP_DELETE\n"
		"unit 'u' before label 'lsize' (chain 't.pre') op OP_DELETE\n"
		"table size 2\n"
		"unit 'u' before label 't.out' op OP_DELETE\n"
		"unit 'u' before label 'lsize' (chain 't.out') op OP_DELETE\n"
		"table size 1\n"
		"unit 'u' before label 't.pre' op OP_INSERT\n"
		"unit 'u' before label 'lsize' (chain 't.pre') op OP_INSERT\n"
		"table size 1\n"
		"unit 'u' before label 't.out' op OP_INSERT\n"
		"unit 'u' before label 'lsize' (chain 't.out') op OP_INSERT\n"
		"table size 2\n"
	);
}

class LabelThrowOnCall : public Label
{
public:
	LabelThrowOnCall(Unit *unit, Onceref<RowType> rtype, const string &name,
			bool *control) :
		Label(unit, rtype, name),
		control_(control)
	{ }

	virtual void execute(Rowop *arg) const
	{
		if (*control_)
			throw Exception("Test throw on call", true);
	}

	bool *control_;
};

class LabelModOnCall : public Label
{
public:
	LabelModOnCall(Unit *unit, Onceref<RowType> rtype, const string &name,
			Table *table, Rowref row) :
		Label(unit, rtype, name),
		table_(table),
		row_(row),
		insert_(false),
		delete_(false)
	{ }

	virtual void execute(Rowop *arg) const
	{
		if (insert_)
			table_->insertRow(row_);
		if (delete_)
			table_->deleteRow(row_);
	}

	Table *table_;
	Rowref row_;
	bool insert_;
	bool delete_;
};

UTESTCASE exceptions(Utest *utest)
{
	string msg;

	Exception::abort_ = false; // make them catchable
	Exception::enableBacktrace_ = false; // make the error messages predictable

	RowType::FieldVec fld;
	mkfields(fld);

	Autoref<Unit> unit = new Unit("u");
	Autoref<Unit::StringNameTracer> trace = new Unit::StringNameTracer;
	unit->setTracer(trace);

	Autoref<RowType> rt1 = new CompactRowType(fld);
	UT_ASSERT(rt1->getErrors().isNull());

	Autoref<TableType> tt = (new TableType(rt1))
		->addSubIndex("primary", new HashedIndexType(
			(new NameSet())->add("b")->add("c"))
		)->addSubIndex("limit", (new FifoIndexType())
			->setLimit(2) // will policy-delete 2 rows
		);

	UT_ASSERT(tt);
	tt->initialize();
	UT_ASSERT(tt->getErrors().isNull());
	UT_ASSERT(!tt->getErrors()->hasError());

	Autoref<Table> t = tt->makeTable(unit, Table::EM_CALL, "t");
	UT_ASSERT(!t.isNull());

	bool throwPre = false, throwOut = false;
	Autoref<Label> labPre = new LabelThrowOnCall(unit, rt1, "labPre", &throwPre);
	Autoref<Label> labOut = new LabelThrowOnCall(unit, rt1, "labOut", &throwOut);

	t->getPreLabel()->chain(labPre);
	t->getLabel()->chain(labOut);

	FdataVec dv;
	mkfdata(dv);
	
	int32_t one32 = 1, two32 = 2;
	int64_t one64 = 1, two64 = 2;

	dv[1].data_ = (char *)&one32; dv[2].data_ = (char *)&one64;
	Rowref r11(rt1,  rt1->makeRow(dv));
	Rhref rh11(t, t->makeRowHandle(r11));

	dv[1].data_ = (char *)&one32; dv[2].data_ = (char *)&two64;
	Rowref r12(rt1,  rt1->makeRow(dv));
	Rhref rh12(t, t->makeRowHandle(r12));

	dv[1].data_ = (char *)&two32; dv[2].data_ = (char *)&one64;
	Rowref r21(rt1,  rt1->makeRow(dv));
	Rhref rh21(t, t->makeRowHandle(r21));

	// start by adding some rows
	UT_ASSERT(t->insert(rh11));
	UT_ASSERT(t->insert(rh12));

	// throw in the insert .pre when flushing 2 rows
	throwPre = true;
	msg.clear();
	try {
		t->insert(rh21);
	} catch (Exception e) {
		msg = e.getErrors()->print();
	}
	throwPre = false;
	UT_IS(msg, 
		"Test throw on call\n"
		"Called through the label 'labPre'.\n"
		"Called chained from the label 't.pre'.\n");
	UT_ASSERT(rh11->isInTable());
	UT_ASSERT(rh12->isInTable());
	UT_ASSERT(!rh21->isInTable());

	// throw in the insert .out when flushing 2 rows
	throwOut = true;
	msg.clear();
	try {
		t->insert(rh21);
	} catch (Exception e) {
		msg = e.getErrors()->print();
	}
	throwOut = false;
	UT_IS(msg, 
		"Test throw on call\n"
		"Called through the label 'labOut'.\n"
		"Called chained from the label 't.out'.\n");
	UT_ASSERT(!rh11->isInTable());
	UT_ASSERT(rh12->isInTable());
	UT_ASSERT(!rh21->isInTable());

	// get back to 2 rows
	UT_ASSERT(t->insert(rh11));

	// throw in insertRow()
	throwPre = true;
	msg.clear();
	try {
		t->insertRow(r21);
	} catch (Exception e) {
		msg = e.getErrors()->print();
	}
	throwPre = false;
	UT_IS(msg, 
		"Test throw on call\n"
		"Called through the label 'labPre'.\n"
		"Called chained from the label 't.pre'.\n");
	UT_ASSERT(rh11->isInTable());
	UT_ASSERT(rh12->isInTable());
	UT_IS(t->size(), 2);

	// throw in the remove .pre
	throwPre = true;
	msg.clear();
	try {
		t->remove(rh11);
	} catch (Exception e) {
		msg = e.getErrors()->print();
	}
	throwPre = false;
	UT_IS(msg, 
		"Test throw on call\n"
		"Called through the label 'labPre'.\n"
		"Called chained from the label 't.pre'.\n");
	UT_ASSERT(rh11->isInTable());
	UT_ASSERT(rh12->isInTable());

	// throw in the remove .out
	throwOut = true;
	msg.clear();
	try {
		t->remove(rh11);
	} catch (Exception e) {
		msg = e.getErrors()->print();
	}
	throwOut = false;
	UT_IS(msg, 
		"Test throw on call\n"
		"Called through the label 'labOut'.\n"
		"Called chained from the label 't.out'.\n");
	UT_ASSERT(!rh11->isInTable());
	UT_ASSERT(rh12->isInTable());

	// throw in the deleteRow
	throwPre = true;
	msg.clear();
	try {
		t->deleteRow(r12);
	} catch (Exception e) {
		msg = e.getErrors()->print();
	}
	throwPre = false;
	UT_IS(msg, 
		"Test throw on call\n"
		"Called through the label 'labPre'.\n"
		"Called chained from the label 't.pre'.\n");
	UT_ASSERT(rh12->isInTable());

	// label for recursive mods
	Autoref<LabelModOnCall> labRec = new LabelModOnCall(unit, rt1, "labRec", t, r21);
	t->getLabel()->chain(labRec);

	// detect a recursive mod on insert (pair with recursive delete)
	labRec->delete_ = true;
	msg.clear();
	try {
		t->insert(rh21);
	} catch (Exception e) {
		msg = e.getErrors()->print();
	}
	labRec->delete_ = false;
	UT_IS(msg, 
		"Detected a recursive modification of the table 't'.\n"
		"Called through the label 'labRec'.\n"
		"Called chained from the label 't.out'.\n");
	UT_ASSERT(rh12->isInTable());
	UT_ASSERT(rh21->isInTable());

	// detect a recursive mod on delete (pair with recursive insert)
	labRec->insert_ = true;
	msg.clear();
	try {
		t->remove(rh21);
	} catch (Exception e) {
		msg = e.getErrors()->print();
	}
	labRec->insert_ = false;
	UT_IS(msg, 
		"Detected a recursive modification of the table 't'.\n"
		"Called through the label 'labRec'.\n"
		"Called chained from the label 't.out'.\n");
	UT_ASSERT(rh12->isInTable());
	UT_ASSERT(!rh21->isInTable());

	Exception::abort_ = true; // restore back
	Exception::enableBacktrace_ = true; // restore back
}

UTESTCASE groupSize(Utest *utest)
{
	string msg;

	Exception::abort_ = false; // make them catchable
	Exception::enableBacktrace_ = false; // make the error messages predictable

	RowType::FieldVec fld;
	mkfields(fld);

	Autoref<Unit> unit = new Unit("u");
	Autoref<Unit::StringNameTracer> trace = new Unit::StringNameTracer;
	unit->setTracer(trace);

	Autoref<RowType> rt1 = new CompactRowType(fld);
	UT_ASSERT(rt1->getErrors().isNull());

	Autoref<TableType> tt = (new TableType(rt1))
		->addSubIndex("bc", (new HashedIndexType(
			(new NameSet())->add("b")->add("c"))
			)->addSubIndex("fifo", (new FifoIndexType()))
		);

	UT_ASSERT(tt);
	tt->initialize();
	UT_ASSERT(tt->getErrors().isNull());
	UT_ASSERT(!tt->getErrors()->hasError());

	Autoref<IndexType> bcixt = tt->findSubIndex("bc");
	UT_ASSERT(bcixt);

	Autoref<Table> t = tt->makeTable(unit, Table::EM_CALL, "t");
	UT_ASSERT(!t.isNull());

	FdataVec dv;
	mkfdata(dv);
	
	int32_t one32 = 1;
	int64_t one64 = 1, two64 = 2;

	dv[1].data_ = (char *)&one32; dv[2].data_ = (char *)&one64;
	Rowref r11(rt1,  rt1->makeRow(dv));
	Rhref rh11(t, t->makeRowHandle(r11));

	dv[1].data_ = (char *)&one32; dv[2].data_ = (char *)&two64;
	Rowref r12(rt1,  rt1->makeRow(dv));
	Rhref rh12(t, t->makeRowHandle(r12));

	UT_IS(t->groupSizeIdx(bcixt, rh11), 0);

	UT_ASSERT(t->insert(rh11));
	UT_IS(t->groupSizeIdx(bcixt, rh11), 1);
	UT_ASSERT(t->insertRow(r11));
	UT_IS(t->groupSizeIdx(bcixt, rh11), 2);

	UT_ASSERT(t->insertRow(r12));
	UT_ASSERT(t->insertRow(r12));
	UT_ASSERT(t->insertRow(r12));
	UT_IS(t->groupSizeIdx(bcixt, rh12), 3);
	UT_IS(t->groupSizeRowIdx(bcixt, r12), 3);
}

