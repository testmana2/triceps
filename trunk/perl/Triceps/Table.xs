//
// This file is a part of Triceps.
// See the file COPYRIGHT for the copyright notice and license information
//
// The wrapper for Table.

#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"

#include "ppport.h"

#include "TricepsPerl.h"

namespace Triceps
{
namespace TricepsPerl 
{

// Parse the argument as either a RowHandle (then return it directly)
// or a Row (then create a RowHandle from it). On errors returns NULL
// and sets the message.
// @patab tab - table where the handle will be used
// @param funcName - calling function name, for error messages
// @param arg - the incoming argument
// @return - a RowHandle, or NULL on error; put it into Rhref because handle may be just created!!!
RowHandle *parseRowOrHandle(Table *tab, const char *funcName, SV *arg)
{
	if( sv_isobject(arg) && (SvTYPE(SvRV(arg)) == SVt_PVMG) ) {
		WrapRowHandle *wrh = (WrapRowHandle *)SvIV((SV*)SvRV( arg ));
		if (wrh == 0) {
			setErrMsg( string(funcName) + ": row argument is NULL and not a valid SV reference to Row or RowHandle" );
			return NULL;
		}
		if (!wrh->badMagic()) {
			if (wrh->ref_.getTable() != tab) {
				setErrMsg( strprintf("%s: row argument is a RowHandle in a wrong table %s",
					funcName, wrh->ref_.getTable()->getName().c_str()) );
				return NULL;
			}
			return wrh->get();
		}
		WrapRow *wr = (WrapRow *)wrh;
		if (wr->badMagic()) {
			setErrMsg( string(funcName) + ": row argument has an incorrect magic for Row or RowHandle" );
			return NULL;
		}

		Row *r = wr->get();
		RowType *rt = wr->ref_.getType();

		if (!rt->equals(tab->getRowType())) {
			string msg = strprintf("%s: table and row types are not equal, in table: ", funcName);
			tab->getRowType()->printTo(msg, NOINDENT);
			msg.append(", in row: ");
			rt->printTo(msg, NOINDENT);

			setErrMsg(msg);
			return NULL;
		}
		return tab->makeRowHandle(r);
	} else{
		setErrMsg( string(funcName) + ": row argument is not a blessed SV reference to Row or RowHandle" );
		return NULL;
	}
}

// Parse the copyTray argument for table ops.
Tray *parseCopyTray(Table *tab, const char *funcName, SV *arg)
{
	if( sv_isobject(arg) && (SvTYPE(SvRV(arg)) == SVt_PVMG) ) {
		WrapTray *wt = (WrapTray *)SvIV((SV*)SvRV( arg ));
		if (wt == 0 || wt->badMagic()) {
			setErrMsg( string(funcName) + ": copyTray has an incorrect magic for WrapTray" );
			return NULL;
		}
		if (wt->getParent() != tab->getUnit()) {
			setErrMsg( strprintf("%s: copyTray is from a wrong unit %s, table in unit %s", funcName,
				wt->getParent()->getName().c_str(), tab->getUnit()->getName().c_str()) );
			return NULL;
		}
		return wt->get();
	} else{
		setErrMsg( string(funcName) + ": copyTray is not a blessed SV reference to WrapTray" );
		return NULL;
	}
}

}; // Triceps::TricepsPerl
}; // Triceps

MODULE = Triceps::Table		PACKAGE = Triceps::Table
###################################################################################

void
DESTROY(WrapTable *self)
	CODE:
		// warn("Table destroyed!");
		delete self;


# The table gets created by Unit::makeTable

WrapLabel *
getInputLabel(WrapTable *self)
	CODE:
		// for casting of return value
		static char CLASS[] = "Triceps::Label";

		clearErrMsg();
		Table *t = self->get();
		RETVAL = new WrapLabel(t->getInputLabel());
	OUTPUT:
		RETVAL

# since the C++ inheritance doesn't propagate to Perl, the inherited call getLabel()
# becomes an explicit getOutputLabel()
WrapLabel *
getOutputLabel(WrapTable *self)
	CODE:
		// for casting of return value
		static char CLASS[] = "Triceps::Label";

		clearErrMsg();
		Table *t = self->get();
		RETVAL = new WrapLabel(t->getLabel());
	OUTPUT:
		RETVAL

WrapTableType *
getType(WrapTable *self)
	CODE:
		// for casting of return value
		static char CLASS[] = "Triceps::TableType";

		clearErrMsg();
		Table *t = self->get();
		RETVAL = new WrapTableType(const_cast<TableType *>(t->getType()));
	OUTPUT:
		RETVAL

WrapUnit*
getUnit(WrapTable *self)
	CODE:
		clearErrMsg();
		Table *tab = self->get();

		// for casting of return value
		static char CLASS[] = "Triceps::Unit";
		RETVAL = new WrapUnit(tab->getUnit());
	OUTPUT:
		RETVAL

# check whether both refs point to the same type object
int
same(WrapTable *self, WrapTable *other)
	CODE:
		clearErrMsg();
		Table *t = self->get();
		Table *ot = other->get();
		RETVAL = (t == ot);
	OUTPUT:
		RETVAL

WrapRowType *
getRowType(WrapTable *self)
	CODE:
		clearErrMsg();
		Table *tab = self->get();

		// for casting of return value
		static char CLASS[] = "Triceps::RowType";
		RETVAL = new WrapRowType(const_cast<RowType *>(tab->getRowType()));
	OUTPUT:
		RETVAL

char *
getName(WrapTable *self)
	CODE:
		clearErrMsg();
		Table *t = self->get();
		RETVAL = (char *)t->getName().c_str();
	OUTPUT:
		RETVAL

# this may be 64-bit, and IV is guaranteed to be pointer-sized
IV
size(WrapTable *self)
	CODE:
		clearErrMsg();
		Table *t = self->get();
		RETVAL = t->size();
	OUTPUT:
		RETVAL

WrapRowHandle *
makeRowHandle(WrapTable *self, WrapRow *row)
	CODE:
		static char funcName[] =  "Triceps::Table::makeRowHandle";
		// for casting of return value
		static char CLASS[] = "Triceps::RowHandle";

		clearErrMsg();
		Table *t = self->get();
		Row *r = row->get();
		RowType *rt = row->ref_.getType();

		if (!rt->equals(t->getRowType())) {
			string msg = strprintf("%s: table and row types are not equal, in table: ", funcName);
			t->getRowType()->printTo(msg, NOINDENT);
			msg.append(", in row: ");
			rt->printTo(msg, NOINDENT);

			setErrMsg(msg);
			XSRETURN_UNDEF;
		}

		RETVAL = new WrapRowHandle(t, t->makeRowHandle(r));
	OUTPUT:
		RETVAL

# XXX test the methods below

# returns: 1 on success, 0 if the policy didn't allow the insert, undef on an error
int
insert(WrapTable *self, SV *rowarg, ...)
	CODE:
		static char funcName[] =  "Triceps::Table::insert";
		if (items != 2 && items != 3)
		   Perl_croak(aTHX_ "Usage: %s(self, rowarg [, copyTray])", funcName);

		clearErrMsg();
		Table *t = self->get();

		Rhref rhr(t,  parseRowOrHandle(t, funcName, rowarg));
		if (rhr.isNull())
			XSRETURN_UNDEF;

		Tray *ctr = NULL;
		if (items == 3) {
			ctr = parseCopyTray(t, funcName, ST(2));
			if (ctr ==  NULL)
				XSRETURN_UNDEF;
		}

		RETVAL = t->insert(rhr.get(), ctr);
	OUTPUT:
		RETVAL

# returns 1 normally, undef on incorrect arguments
int
remove(WrapTable *self, WrapRowHandle *wrh, ...)
	CODE:
		static char funcName[] =  "Triceps::Table::remove";
		if (items != 2 && items != 3)
		   Perl_croak(aTHX_ "Usage: %s(self, rowHandle [, copyTray])", funcName);

		clearErrMsg();
		Table *t = self->get();
		RowHandle *rh = wrh->get();

		if (wrh->ref_.getTable() != t) {
			setErrMsg( strprintf("%s: row argument is a RowHandle in a wrong table %s",
				funcName, wrh->ref_.getTable()->getName().c_str()) );
			XSRETURN_UNDEF;
		}

		Tray *ctr = NULL;
		if (items == 3) {
			ctr = parseCopyTray(t, funcName, ST(2));
			if (ctr ==  NULL)
				XSRETURN_UNDEF;
		}

		t->remove(rh, ctr);
		RETVAL = 1;
	OUTPUT:
		RETVAL

# XXX add the rest of methods

