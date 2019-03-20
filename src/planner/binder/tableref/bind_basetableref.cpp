#include "planner/binder.hpp"
#include "parser/tableref/basetableref.hpp"
#include "parser/tableref/subqueryref.hpp"
#include "planner/tableref/bound_basetableref.hpp"
#include "main/client_context.hpp"

using namespace duckdb;
using namespace std;

unique_ptr<BoundTableRef> Binder::Bind(BaseTableRef &expr) {
	// CTEs and views are also referred to using BaseTableRefs, hence need to distinguish here
	// check if the table name refers to a CTE
	auto cte = FindCTE(expr.table_name);
	if (cte) {
		// it does! create a subquery with a copy of the CTE and resolve it
		SubqueryRef subquery(move(cte));
		subquery.alias = expr.alias.empty() ? expr.table_name : expr.alias;
		return Bind(subquery);
	}
	// not a CTE
	// extract a table or view from the catalog
	auto table_or_view = context.db.catalog.GetTableOrView(context.ActiveTransaction(),
		expr.schema_name, expr.table_name);
	switch (table_or_view->type) {
	case CatalogType::TABLE: {
		// base table: create the BoundBaseTableRef node
		auto table = (TableCatalogEntry*) table_or_view;
		size_t table_index = GenerateTableIndex();
		auto result = make_unique<BoundBaseTableRef>(table, table_index);
		bind_context.AddBaseTable(result.get(),
		                          expr.alias.empty() ? expr.table_name : expr.alias);
		return move(result);
	}
	case CatalogType::VIEW: {
		auto view_catalog_entry = (ViewCatalogEntry *)table_or_view;
		SubqueryRef subquery(view_catalog_entry->query->Copy());
		subquery.alias = expr.alias.empty() ? expr.table_name : expr.alias;

		// if we have subquery aliases we need to set them for the subquery. However, there may be non-aliased result
		// cols from the subquery. Those are returned as well, but are not renamed.
		auto &select_list = subquery.subquery->GetSelectList();
		if (view_catalog_entry->aliases.size() > 0) {
			subquery.column_name_alias.resize(select_list.size());
			for (size_t col_idx = 0; col_idx < select_list.size(); col_idx++) {
				if (col_idx < view_catalog_entry->aliases.size()) {
					subquery.column_name_alias[col_idx] = view_catalog_entry->aliases[col_idx];
				} else {
					subquery.column_name_alias[col_idx] = select_list[col_idx]->GetName();
				}
			}
		}
		return Bind(subquery);
	}
	default:
		throw NotImplementedException("Catalog entry type");
	}
}
