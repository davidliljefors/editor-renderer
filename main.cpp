#include "Editor.h"

#include "Core/TempAllocator.h"

#pragma comment(lib, "user32.lib")

Allocator* GLOBAL_HEAP;

i32 main()
{
	HeapAllocator gHeap;
	GLOBAL_HEAP = &gHeap;

	block_memory_init();

	EditorApp* app = create<EditorApp>(GLOBAL_HEAP, GLOBAL_HEAP);
	app->run();

	block_memory_shutdown();

	return 0;
}