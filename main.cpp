#include <cstdio>

#include "Editor.h"
#include "DynamicData.h"
#include "murmurhash.inl"
#include "Random.h"

#include "Core/TempAllocator.h"

#include <windows.h>

#pragma comment(lib, "user32.lib")

Allocator* GLOBAL_HEAP;

#include "DynamicTypes.h"

void load_dynamicdata_directory(const char* directory)
{
	char searchPath[MAX_PATH];
	snprintf(searchPath, MAX_PATH, "%s\\*.*", directory);

	WIN32_FIND_DATAA findData;
	HANDLE hFind = FindFirstFileA(searchPath, &findData);

	if (hFind == INVALID_HANDLE_VALUE) {
		printf("Error opening directory: %s\n", directory);
		return;
	}

	int count = 0;

	TempAllocator ta;
	Array<Unresolved> unresolveds;
	unresolveds.set_allocator(&ta);

	do
	{
		if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
		{
			DynamicData created;
			char subpath[MAX_PATH];
			snprintf(subpath, MAX_PATH, "%s/%s", directory, findData.cFileName);
			if (DynamicData_deserialize_json_file(subpath, &created, &unresolveds))
			{
				++count;
				Debug_register_root_object(created);
			}
		}

	} while (FindNextFileA(hFind, &findData));

	DynamicData_resolve_unresolved(&unresolveds);

	printf("Loaded %d items into DynamicData", count);

	FindClose(hFind);
}

i32 main()
{
	HeapAllocator gHeap;
	GLOBAL_HEAP = &gHeap;

	Random_initialize_context();

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

	load_dynamicdata_directory("entities");


	app->run();

	DynamicData_shutdown();

	block_memory_shutdown();

	return 0;
}