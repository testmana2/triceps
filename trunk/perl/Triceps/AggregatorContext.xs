//
// This file is a part of Triceps.
// See the file COPYRIGHT for the copyright notice and license information
//
// The context for an aggregator handler call.

#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"

#include "ppport.h"

#include "TricepsPerl.h"
#include "WrapAggregatorContext.h"

// The idea here is to combine multiple C++ structures that are used only in
// an aggregator handler call into a insgle Perl object, thus simplifying the
// API for the Perl aggregators.

namespace Triceps
{
namespace TricepsPerl 
{

WrapMagic magicWrapAggregatorContext = { "AggCtx" };

}; // Triceps::TricepsPerl
}; // Triceps

MODULE = Triceps::AggregatorContext		PACKAGE = Triceps::AggregatorContext
###################################################################################

# can not use the common typemap, because the destruction can be legally
# called on an invalidated object, which would not pass the typemap
void
DESTROY(SV *selfsv)
	CODE:
		WrapAggregatorContext *self;

		if( sv_isobject(selfsv) && (SvTYPE(SvRV(selfsv)) == SVt_PVMG) ) {
			self = (WrapAggregatorContext *)SvIV((SV*)SvRV( selfsv ));
			if (self == 0 || self->badMagic()) {
				warn( "Triceps::AggregatorContext::DESTROY: self has an incorrect magic for WrapAggregatorContext" );
				XSRETURN_UNDEF;
			}
		} else{
			warn( "Triceps::AggregatorContext::DESTROY: self is not a blessed SV reference to WrapAggregatorContext" );
			XSRETURN_UNDEF;
		}
		// warn("AggregatorContext %p destroyed!", self);
		delete self;

# XXX test the methods below here

# get the number of rows in the group
int
groupSize(WrapAggregatorContext *self)
	CODE:
		clearErrMsg();
		RETVAL = self->getParentIdxType()->groupSize(self->getGroupHandle());
	OUTPUT:
		RETVAL

# iteration on the group
# RowHandle with NULL pointer in it is used for the end-iterator

WrapRowHandle *
begin(WrapAggregatorContext *self)
	CODE:
		static char CLASS[] = "Triceps::RowHandle";

		clearErrMsg();
		Table *t = self->getTable();
		Index *idx = self->getIndex();
		RETVAL = new WrapRowHandle(t, idx->begin());
	OUTPUT:
		RETVAL
		
WrapRowHandle *
next(WrapAggregatorContext *self, WrapRowHandle *wcur)
	CODE:
		static char CLASS[] = "Triceps::RowHandle";

		clearErrMsg();
		Table *t = self->getTable();
		Index *idx = self->getIndex();
		RowHandle *cur = wcur->get(); // NULL is OK

		static char funcName[] =  "Triceps::AggregatorContext::next";
		if (wcur->ref_.getTable() != t) {
			setErrMsg( strprintf("%s: row argument is a RowHandle in a wrong table %s",
				funcName, wcur->ref_.getTable()->getName().c_str()) );
			XSRETURN_UNDEF;
		}

		RETVAL = new WrapRowHandle(t, idx->next(cur));
	OUTPUT:
		RETVAL
		
# XXX add translation to a sibling index, for iteration on it

# XXX add send()
