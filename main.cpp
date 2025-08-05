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

extern void Debug_register_component_type(i32);

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
	Array<DynamicData_UnresolvedObject> unresolveds;
	unresolveds.set_allocator(&ta);

	do
	{
		if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
		{
			dd_id_t created;
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

	printf("Loaded %d items into DynamicData\n", count);

	FindClose(hFind);
}

void register_common_types()
{
	const DynamicDataPropertyDef vec3_def[] = {
		makeProperty("x", DynamicValue::Type_Number),
		makeProperty("y", DynamicValue::Type_Number),
		makeProperty("z", DynamicValue::Type_Number),
	};

	const DynamicDataPropertyDef quat_def[] = {
		makeProperty("x", DynamicValue::Type_Number),
		makeProperty("y", DynamicValue::Type_Number),
		makeProperty("z", DynamicValue::Type_Number),
		makeProperty("w", DynamicValue::Type_Number),
	};

	DynamicData_register_type("vec3", "Vec3", vec3_def, DD_ARRAY_COUNT(vec3_def));
	DynamicData_register_type("quat", "Quaternion", quat_def, DD_ARRAY_COUNT(quat_def));

	const DynamicDataPropertyDef transform_def[] = {
		makeProperty("pos", DynamicValue::Type_Object, TM_STATIC_HASH("vec3", 0x80edf8ab6760fa67ULL)),
		makeProperty("rot", DynamicValue::Type_Object, TM_STATIC_HASH("quat", 0x4134388ea9bde8c0ULL)),
		makeProperty("scale", DynamicValue::Type_Object, TM_STATIC_HASH("vec3", 0x80edf8ab6760fa67ULL)),
	};

	DynamicData_register_type("transform", "Transform", transform_def, DD_ARRAY_COUNT(transform_def));
}

void register_component_types()
{
	const DynamicDataPropertyDef transform_comp_def[] = {
		makeProperty("transform", DynamicValue::Type_Object, TM_STATIC_HASH("transform", 0x69e14b13ad9b5315ULL)),
	};

	const DynamicDataPropertyDef spline_comp_def[] = {
		makeProperty("points", DynamicValue::Type_Set, TM_STATIC_HASH("vec3", 0x80edf8ab6760fa67ULL)),
	};

	i32 id = DynamicData_register_type("transform_component", "Trasnform Component", transform_comp_def, DD_ARRAY_COUNT(transform_comp_def));
	Debug_register_component_type(id);

	id = DynamicData_register_type("spline_component", "Spline Component", spline_comp_def, DD_ARRAY_COUNT(spline_comp_def));
	Debug_register_component_type(id);
}

i32 main()
{
	HeapAllocator gHeap;
	GLOBAL_HEAP = &gHeap;

	Random_initialize_context();

	DynamicData_initialize(GLOBAL_HEAP);

	const DynamicDataPropertyDef entity_props[] = {
		makeProperty("name", DynamicValue::Type_String),
		makeProperty("children", DynamicValue::Type_Set, TM_STATIC_HASH("entity", 0x9831ca893b0d087dULL)),
		makeProperty("components", DynamicValue::Type_Set),
	};

	DynamicData_register_type("entity", "Entity", entity_props, DD_ARRAY_COUNT(entity_props));

	block_memory_init();

	EditorApp* app = new (GLOBAL_HEAP) EditorApp(GLOBAL_HEAP);

	register_common_types();
	register_component_types();

	load_dynamicdata_directory("entities");

	app->run();

	DynamicData_shutdown();

	block_memory_shutdown();



	return 0;
}