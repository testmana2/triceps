//
// (C) Copyright 2011-2015 Sergey A. Babkin.
// This file is a part of Triceps.
// See the file COPYRIGHT for the copyright notice and license information
//
//
// Test of the ordered index.

#include <utest/Utest.h>
#include <string.h>

#include <type/AllTypes.h>
#include <sched/AggregatorGadget.h>
#include <common/StringUtil.h>
#include <common/Exception.h>
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

// The basic tests are similar to those in t_TableType for the other index types.
// Also see the copy tests in t_TableType.

UTESTCASE orderedIndex(Utest *utest)
{
	RowType::FieldVec fld;
	mkfields(fld);

	Autoref<RowType> rt1 = new CompactRowType(fld);
	UT_ASSERT(rt1->getErrors().isNull());

	Autoref<TableType> tt = (new TableType(rt1))
		->addSubIndex("primary", new OrderedIndexType(
			(new NameSet())->add("a")->add("!e"))
		);

	UT_ASSERT(tt);
	tt->initialize();
	UT_ASSERT(tt->getErrors().isNull());
	UT_ASSERT(!tt->getErrors()->hasError());

	// repeated initialization should be fine
	tt->initialize();
	UT_ASSERT(tt->getErrors().isNull());
	UT_ASSERT(!tt->getErrors()->hasError());

	const char *expect =
		"table (\n"
		"  row {\n"
		"    uint8[10] a,\n"
		"    int32[] b,\n"
		"    int64 c,\n"
		"    float64 d,\n"
		"    string e,\n"
		"  }\n"
		") {\n"
		"  index OrderedIndex(a, !e, ) primary,\n"
		"}"
	;
	if (UT_ASSERT(tt->print() == expect)) {
		printf("---Expected:---\n%s\n", expect);
		printf("---Received:---\n%s\n", tt->print().c_str());
		printf("---\n");
		fflush(stdout);
	}
	UT_IS(tt->print(NOINDENT), "table ( row { uint8[10] a, int32[] b, int64 c, float64 d, string e, } ) { index OrderedIndex(a, !e, ) primary, }");

	// get back the initialized types
	IndexType *prim = tt->findSubIndex("primary");
	UT_ASSERT(prim != NULL);
	UT_IS(tt->findSubIndexById(IndexType::IT_ORDERED), prim);
	UT_IS(tt->getFirstLeaf(), prim);

	{
		Autoref<NameSet> expectKey = (new NameSet())->add("a")->add("e");
		const NameSet *key = prim->getKey();
		UT_ASSERT(key != NULL);
		UT_ASSERT(key->equals(expectKey));
	}
	{
		Autoref<NameSet> expectKey = (new NameSet())->add("a")->add("!e");
		const NameSet *key = prim->getKeyExpr();
		UT_ASSERT(key != NULL);
		UT_ASSERT(key->equals(expectKey));
	}

	UT_IS(tt->findSubIndexById(IndexType::IT_LAST), NULL);
	UT_IS(tt->findSubIndex("nosuch"), NULL);

	UT_IS(prim->findSubIndex("nosuch"), NULL);
	UT_IS(prim->findSubIndex("nosuch")->findSubIndex("nothat"), NULL);
	UT_IS(prim->findSubIndexById(IndexType::IT_LAST), NULL);
	UT_IS(prim->findSubIndex("nosuch")->findSubIndexById(IndexType::IT_LAST), NULL);
}

UTESTCASE orderedNested(Utest *utest)
{
	RowType::FieldVec fld;
	mkfields(fld);

	Autoref<RowType> rt1 = new CompactRowType(fld);
	UT_ASSERT(rt1->getErrors().isNull());

	Autoref<TableType> tt = initializeOrThrow(TableType::make(rt1)
		->addSubIndex("primary", (new OrderedIndexType(
			(new NameSet())->add("a")->add("!e")))
			->addSubIndex("level2", new OrderedIndexType(
				(new NameSet())->add("!a")->add("e"))
			)
		)
	);

	UT_ASSERT(tt);
	if (UT_ASSERT(tt->getErrors().isNull()))
		return;
	
	const char *expect =
		"table (\n"
		"  row {\n"
		"    uint8[10] a,\n"
		"    int32[] b,\n"
		"    int64 c,\n"
		"    float64 d,\n"
		"    string e,\n"
		"  }\n"
		") {\n"
		"  index OrderedIndex(a, !e, ) {\n"
		"    index OrderedIndex(!a, e, ) level2,\n"
		"  } primary,\n"
		"}"
	;
	if (UT_ASSERT(tt->print() == expect)) {
		printf("---Expected:---\n%s\n", expect);
		printf("---Received:---\n%s\n", tt->print().c_str());
		printf("---\n");
		fflush(stdout);
	}
	UT_IS(tt->print(NOINDENT), "table ( row { uint8[10] a, int32[] b, int64 c, float64 d, string e, } ) { index OrderedIndex(a, !e, ) { index OrderedIndex(!a, e, ) level2, } primary, }");

	// get back the initialized types
	IndexType *prim = tt->findSubIndex("primary");
	if (UT_ASSERT(prim != NULL))
		return;
	UT_IS(tt->findSubIndexById(IndexType::IT_ORDERED), prim);

	IndexType *sec = prim->findSubIndex("level2");
	if (UT_ASSERT(sec != NULL))
		return;
	UT_IS(prim->getTabtype(), tt);
	UT_IS(prim->findSubIndexById(IndexType::IT_ORDERED), sec);

	UT_IS(sec->findSubIndex("nosuch"), NULL);
	UT_IS(sec->findSubIndexById(IndexType::IT_LAST), NULL);
}

UTESTCASE orderedBadField(Utest *utest)
{
	RowType::FieldVec fld;
	mkfields(fld);

	Autoref<RowType> rt1 = new CompactRowType(fld);
	UT_ASSERT(rt1->getErrors().isNull());

	Autoref<TableType> tt = (new TableType(rt1))
		->addSubIndex("primary", new OrderedIndexType(
			(new NameSet())->add("!x")->add("e"))
		)
		;

	UT_ASSERT(tt);
	tt->initialize();
	if (UT_ASSERT(!tt->getErrors().isNull()))
		return;
	UT_ASSERT(tt->getErrors()->hasError());
	UT_IS(tt->getErrors()->print(), "index error:\n  nested index 1 'primary':\n    can not find the key field 'x'\n");
}

// The tests similar to t_xSortedIndexType
