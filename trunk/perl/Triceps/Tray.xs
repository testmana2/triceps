//
// This file is a part of Triceps.
// See the file COPYRIGHT for the copyright notice and license information
//
// The wrapper for Tray.

#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"

#include "ppport.h"

#include "TricepsPerl.h"

MODULE = Triceps::Tray		PACKAGE = Triceps::Tray
###################################################################################

void
DESTROY(WrapTray *self)
	CODE:
		// warn("Tray destroyed!");
		delete self;

# Since in C++ a tray is simply a deque, instead of providing all the methods, just
# provide a conversion to and from array

# Constructed in Unit::makeTray

# check whether both refs point to the same type object
int
same(WrapTray *self, WrapTray *other)
	CODE:
		clearErrMsg();
		Tray *t = self->get();
		Tray *ot = other->get();
		RETVAL = (t == ot);
	OUTPUT:
		RETVAL

WrapUnit*
getUnit(WrapTray *self)
	CODE:
		clearErrMsg();

		// for casting of return value
		static char CLASS[] = "Triceps::Unit";
		RETVAL = new WrapUnit(self->getParent());
	OUTPUT:
		RETVAL

# make a copy 
WrapTray *
copy(WrapTray *self)
	CODE:
		// for casting of return value
		static char CLASS[] = "Triceps::Tray";
		clearErrMsg();
		Tray *t = self->get();
		RETVAL = new WrapTray(self->getParent(), new Tray(*t));
	OUTPUT:
		RETVAL

SV *
toArray(WrapTray *self)
	PPCODE:
		clearErrMsg();
		Tray *tray = self->get();
		
		// for casting of return value
		static char CLASS[] = "Triceps::Rowop";

		int nf = tray->size();
		for (int i = 0; i < nf; i++) {
			SV *ropv = sv_newmortal();
			sv_setref_pv( ropv, "Triceps::Rowop", (void*)(new WrapRowop((*tray)[i])) );
			XPUSHs(ropv);
		}

void *
clear(WrapTray *self)
	CODE:
		clearErrMsg();
		Tray *t = self->get();
		t->clear();

# returns itself (or undef on error)
# (the code is almost the same as Triceps::Unit::makeTray)
SV *
push(WrapTray *self, ...)
	CODE:
		char funcName[] = "Triceps::Tray::push";
		// for casting of return value
		static char CLASS[] = "Triceps::Tray";

		clearErrMsg();
		Unit *unit = self->getParent();
		Tray *tray = self->get();

		for (int i = 1; i < items; i++) {
			SV *arg = ST(i);
			if( sv_isobject(arg) && (SvTYPE(SvRV(arg)) == SVt_PVMG) ) {
				WrapRowop *var = (WrapRowop *)SvIV((SV*)SvRV( arg ));
				if (var == 0 || var->badMagic()) {
					setErrMsg( strprintf("%s: argument %d has an incorrect magic for Rowop", funcName, i) );
					XSRETURN_UNDEF;
				}
				if (var->get()->getLabel()->getUnit() != unit) {
					setErrMsg( strprintf("%s: argument %d is a Rowop for label %s from a wrong unit %s", funcName, i,
						var->get()->getLabel()->getName().c_str(), var->get()->getLabel()->getUnit()->getName().c_str()) );
					XSRETURN_UNDEF;
				}
			} else{
				setErrMsg( strprintf("%s: argument %d is not a blessed SV reference to Rowop", funcName, i) );
				XSRETURN_UNDEF;
			}
		}

		for (int i = 1; i < items; i++) {
			SV *arg = ST(i);
			WrapRowop *var = (WrapRowop *)SvIV((SV*)SvRV( arg ));
			tray->push_back(var->get());
		}
		SvREFCNT_inc(ST(0));
		RETVAL = ST(0);
	OUTPUT:
		RETVAL

# XXX add tests
