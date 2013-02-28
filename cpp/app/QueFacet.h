//
// (C) Copyright 2011-2013 Sergey A. Babkin.
// This file is a part of Triceps.
// See the file COPYRIGHT for the copyright notice and license information
//
//
// Various bits and pieces for the facet queues.

#ifndef __Triceps_QueFacet_h__
#define __Triceps_QueFacet_h__

#include <deque>
#include <common/Common.h>
#include <pw/ptwrap.h>
#include <mem/Atomic.h>
#include <app/Xtray.h>

// XXX work in progress

namespace TRICEPS_NS {

// These small classes logically belong as fragments to the bigger objects
// but had to be separated to keep the reference counting from creating
// the cycles.

// Notification from the Nexuses to the Triead that there is something
// to read. Done as a separate object because the Nexuses will need
// to refrence it, and they can not reference the Tried directly
// because that would cause a reference loop.
// This event covers all the facets connected to the thread.
class QueEvent: public Mtarget
{
public:
	pw::event ev_;
	// XXX add a mutex? have a chainevent, like chaincond?
	vector<bool> ready_; // indication of which facets are ready
	vector<bool> sready_; // of which facets the thread scheduler already knows that they are ready
};

// The queue of one reader facet.
class ReaderQueue: public Mtarget
{
public:
	// Write an Xtray to the first reader in the vector.
	// This generates the sequential id for the Xtray.
	// May sleep if the write queue is full.
	//
	// @param gen - generation of the vector used by the writer
	// @param xt - Xtray being written
	// @param trayId - place to return the generated id of the tray
	// @return - true if the generations matched and the write went through
	//        and generated the id; false if the generations were mismatched
	//        or if this reader is marked as dead, and nothing was done
	bool writeFirst(int gen, Xtray *xt, int32_t &trayId);

	// Write an Xtray with a specific sequence to a reader that is
	// not first in the vector. Does nothing if the queue is dead.
	// May sleep if the write queue is full.
	//
	// @param xt - Xtray being written
	// @param trayId - the sequential id of the tray
	void write(Xtray *xt, int32_t trayId);

protected:
	typedef deque<Autoref<Xtray> > Xdeque;

	Xdeque &writeq()
	{
		return q_[rq_ ^ 1];
	}
	Xdeque &readq()
	{
		return q_[rq_];
	}
	// Insert an Xtray into the write queue at the specified index
	// relative to the start of the queue.
	// Extends the queue as needed. Never blocks.
	// @param xt - Xtray to insert
	// @param idx - index to insert at
	void insertQueL(Xtray *xt, int32_t idx);

	// part that is set once and never changed
	
	Autoref<QueEvent> qev_; // where to signal when have data
	int qidx_; // index of this facet's indication in QueEvent::ready_

	// part that is protected by the mutex

	pw::pmutex mutex_;
	pw::pchaincond condfull_; // wait when the queue is full
	pw::pchaincond condempty_; // wait when the queue is empty

	Xdeque q_[2]; // the queues of trays; they alternate with double buffering;
		// the one currently used for reading can be accessed without a lock
	int rq_; // index of the queue that is currently used for reading
		// changed only by the reader thread, with mutex locked,
		// so that thread 

	// the Xtray ids may roll over
	int32_t prevId_; // id of the last Xtray preceding the start of the queue
	int32_t nextId_; // id of the next Xtray past the end of the queue

	int32_t sizeLimit_; // the high water mark for writing

	int gen_; // the generation of the nexus's reader vector

	bool dead_; // this queue has been disconnected from the nexus

private:
	ReaderQueue(const ReaderQueue &);
	void operator=(const ReaderQueue &);
};

// Collection of the reader facets in a Nexus. The writers will
// be sending data to all of them. Done as a separate object, so that
// all the writers can refer to it.
//
// As the readers are added and deleted from a nexus, the new ReaderVec
// objects are created, each with an increased generation number.
// The addition and deletion are infrequent operations and can afford
// to be expensive.
//
// Like other shared objects, it's all-writes-before-sharing.
class ReaderVec: public Mtarget
{
	friend class Nexus;
public:
	typedef vector<Autoref<ReaderQueue> > Vec;

	// @param g - the generation
	ReaderVec(int g):
		gen_(g)
	{ }
		
	// read the vector
	const Vec &v() const
	{
		return v_;
	}

	// read the generation
	int gen() const
	{
		return gen_;
	}

protected:
	Vec v_; // the Nexus will add directly to it on construction
	int gen_; // the generation of the vector

private:
	ReaderVec();
	ReaderVec(const ReaderVec &);
	void operator=(const ReaderVec &);
};

class NexusWriter: public Mtarget
{
public:
	// Update the new reader vector (readersNew_).
	// @param rv - the new vector (the caller must hold a reference to it
	//        through the call). The writer will discover it on the next
	//        attempt to write.
	void setReaderVec(ReaderVec *rv);

	// Write the Xtray.
	// Called only from the thread that owns this facet.
	//
	// @param xt - the data (the caller must hold a reference to it
	//        through the call, the caller must not change the Xtray contents
	//        afterwards).
	void write(Xtray *xt);

protected:
	Autoref<ReaderVec> readers_; // the current active reader vector

	pw::pmutex mutexNew_; // protects the readersNew_
	Autoref<ReaderVec> readersNew_; // the new reader vector

private:
	NexusWriter(const NexusWriter &);
	void operator=(const NexusWriter &);
};

}; // TRICEPS_NS

#endif // __Triceps_QueFacet_h__