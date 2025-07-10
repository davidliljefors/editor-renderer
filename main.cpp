#include "Editor.h"
#include "Entity.h"

#include "Core/TempAllocator.h"

#pragma comment(lib, "user32.lib")

Allocator* GLOBAL_HEAP;

i32 main()
{
	const DynamicDataPropertyDef test_nested_props[] = {
		{ "nested_float", DynamicData::Type_Number },
		{ "nested_integer", DynamicData::Type_Integer },
	};

	u64 nested_type_hash = DynamicData_register_type("test_nested", test_nested_props, DD_ARRAY_COUNT(test_nested_props));

	const DynamicDataPropertyDef test_subobject_props[] = {
		{ "example_float", DynamicData::Type_Number },
		{ "example_integer", DynamicData::Type_Integer },
		{ "example_subobject", DynamicData::Type_Object, nested_type_hash },
	};

	u64 subobject_type_hash = DynamicData_register_type("test_subobject", test_subobject_props, DD_ARRAY_COUNT(test_subobject_props));

	const DynamicDataPropertyDef entity_props[] = {
		{ "name", DynamicData::Type_String },
		{ "children", DynamicData::Type_Set },
		{ "components", DynamicData::Type_Set },
		{ "dummy_subobject", DynamicData::Type_Object, subobject_type_hash }
	};

	DynamicData_register_type("Entity Type", entity_props, DD_ARRAY_COUNT(entity_props));

	HeapAllocator gHeap;
	GLOBAL_HEAP = &gHeap;

	block_memory_init();

	EditorApp* app = create<EditorApp>(GLOBAL_HEAP, GLOBAL_HEAP);
	app->run();

	block_memory_shutdown();

	return 0;
}