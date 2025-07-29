#include "Editor.h"
#include "Entity.h"
#include "murmurhash.inl"

#include "Core/TempAllocator.h"

#pragma comment(lib, "user32.lib")

Allocator* GLOBAL_HEAP;

#include "DynamicTypes.h"

i32 main()
{
	HeapAllocator gHeap;
	GLOBAL_HEAP = &gHeap;

	DynamicData_initialize(GLOBAL_HEAP);

	const DynamicDataPropertyDef test_nested_props[] = {
		{ "nested_float", DynamicData::Type_Number, 0 },
		{ "nested_integer", DynamicData::Type_Integer, 0 },
	};

	u64 nested_type_hash = DynamicData_register_type("test_nested", test_nested_props, DD_ARRAY_COUNT(test_nested_props));

	const DynamicDataPropertyDef test_subobject_props[] = {
		{ "example_float", DynamicData::Type_Number, 0 },
		{ "example_integer", DynamicData::Type_Integer, 0 },
		{ "example_subobject", DynamicData::Type_Object, nested_type_hash },
	};

	u64 subobject_type_hash = DynamicData_register_type("test_subobject", test_subobject_props, DD_ARRAY_COUNT(test_subobject_props));

	const DynamicDataPropertyDef entity_props[] = {
		{ "name", DynamicData::Type_String, 0 },
		{ "children", DynamicData::Type_Set, ENTITY_NAME_HASH },
		{ "components", DynamicData::Type_Set, 0 },
		{ "dummy_subobject", DynamicData::Type_Object, subobject_type_hash }
	};

	DynamicData_register_type(ENTITY_TYPE_NAME, entity_props, DD_ARRAY_COUNT(entity_props));

	string_repository_hash("fields");

	block_memory_init();

	EditorApp* app = create<EditorApp>(GLOBAL_HEAP, GLOBAL_HEAP);
	app->run();

	DynamicData_shutdown();

	block_memory_shutdown();

	return 0;
}