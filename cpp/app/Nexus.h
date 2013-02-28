//
// (C) Copyright 2011-2013 Sergey A. Babkin.
// This file is a part of Triceps.
// See the file COPYRIGHT for the copyright notice and license information
//
//
// Nexus is a communication point between the threads, a set of labels
// for passing data downstream and upstream.

#ifndef __Triceps_Nexus_h__
#define __Triceps_Nexus_h__

#include <map>
#include <deque>
#include <common/Common.h>
#include <pw/ptwrap.h>
#include <mem/Mtarget.h>
#include <type/RowSetType.h>
#include <type/TableType.h>
#include <app/QueFacet.h>

namespace TRICEPS_NS {

class Facet;
class Triead;

// Nexus is the machinery that keeps a queue of rowops (their inter-thread
// representations) for passing between the threads. The queue is common
// between all the labels and provides a common order for them. More exactly,
// there might be up to two queues: one "downstream" and one "upstream".
// But the initial goal is to have one per nexus.
//
// Besides the queues, a nexus is used to export the assorted row types
// and table types. They live in their separate sub-namespaces.
//
// The Nexuses could live right on the level under App but in case of the
// deadlocks this would make tracing the cause difficult (i.e. thread A
// waits for a nexus to be defined by thread B, while thread B waits for
// a nexus to be defined by thread A). So each Nexus is associated with and 
// nested under a certain Triead.
class Nexus : public Mtarget
{
	friend class App;
	friend class Triead;
	friend class TrieadOwner;
	friend class Facet;
public:
	typedef map<string, Autoref<RowType> > RowTypeMap;
	typedef map<string, Autoref<TableType> > TableTypeMap;

	// Create a Nexus from its first Facet.
	// The types will be deep-copied from the Facet. The Facet must not
	// contain errors, the callexp rmust check it before.
	//
	// @param tname - name of the thread that owns the nexus
	// @param facet - the first facet; must not contain errors
	Nexus(const string &tname, Facet *facet);

	// Get the name
	const string &getName() const
	{
		return name_;
	}

	// Get the name of the thread
	const string &getTrieadName() const
	{
		return tname_;
	}

	// Check whether the nexus is exported.
	bool isExported() const
	{
		return !tname_.empty();
	}

	// Check whether the nexus is reverse, i.e. the its queue is pointed
	// upwards.
	bool isReverse() const
	{
		return reverse_;
	}

	// Check whether the nexus is unicast.
	bool isUnicast() const
	{
		return unicast_;
	}

	// XXX add print() ?
protected:
	// The nexus'es metadata gets defined in one thread and then never changed,
	// so it doesn't need a lock. The actual working queue does need a lock.
	// XXX add the working queue, with a lock

	pw::pmutex mutex_; // mutex controlling the nexus

	string tname_; // name of the thread that owns this nexus
	string name_; // name of the nexus in that thread

	Autoref<RowSetType> type_; // the type of the nexus's main queue
	RowTypeMap rowTypes_; // the collection of row types
	TableTypeMap tableTypes_; // the collection of table types

	// Each writer sends its Xtrays directly to the readers, to avoid
	// competing for a single queue in the Nexus. However
	// all the readers must see the data in the exact same sequence.
	// So each writer gets the sequential id from the Nexus through
	// a quick atomic operation, and then uses this id to put the tray
	// into the proper place in the reader's queue.
	// The ids may wrap around and repeat over time, as long as they
	// don't repeat within the length of one queue.
	AtomicInt trayId_; // sequential ID of the last sent Xtray (starts at 0)

	int nwrite_; // number of writer facets
	int nread_; // number of reader facets

	bool reverse_; // Flag: this nexus's main queue is pointed upwards
	bool unicast_; // Flag: each row goes to only one reader, as opposed to copied to all readers

private:
	Nexus();
	Nexus(const Nexus &);
	void operator=(const Nexus &);
};

}; // TRICEPS_NS

#endif // __Triceps_Nexus_h__