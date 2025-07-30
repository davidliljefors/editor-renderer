#include "Editor.h"
#include "DynamicData.h"
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
		makeProperty("nested_float", DynamicData::Type_Number),
		makeProperty("nested_integer", DynamicData::Type_Integer),
	};

	DynamicData_register_type("test_nested", test_nested_props, DD_ARRAY_COUNT(test_nested_props));

	const DynamicDataPropertyDef test_subobject_props[] = {
		makeProperty("example_float", DynamicData::Type_Number),
		makeProperty("example_integer", DynamicData::Type_Integer),
		makeProperty("example_subobject", DynamicData::Type_Object, TM_STATIC_HASH("test_nested", 0x44186028604f43d3ULL)),
	};

	DynamicData_register_type("test_subobject", test_subobject_props, DD_ARRAY_COUNT(test_subobject_props));

	const DynamicDataPropertyDef entity_props[] = {
		makeProperty("name", DynamicData::Type_String),
		makeProperty("children", DynamicData::Type_Set, ENTITY_NAME_HASH),
		makeProperty("components", DynamicData::Type_Set),
		makeProperty("dummy_subobject", DynamicData::Type_Object, TM_STATIC_HASH("test_subobject", 0x5dfe10daeb51c234ULL)),
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