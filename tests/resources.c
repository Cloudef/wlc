#include <stdlib.h>
#include <assert.h>
#include <wlc/wlc.h>
#include "resources/resources.h"

static bool constructor_called = false;
static bool destructor_called = false;

void
destructor(void *ptr)
{
   assert(ptr);
   destructor_called = true;
}

bool
constructor(void *ptr)
{
   assert(ptr);
   return (constructor_called = true);
}

struct contains_source {
   uint8_t padding[3];
   struct wlc_source source;
   uint8_t padding2[3];
};

void
destructor2(struct contains_source *ptr)
{
   assert(ptr);
   wlc_source_release(&ptr->source);
}

bool
constructor2(struct contains_source *ptr)
{
   assert(ptr);
   assert(wlc_source(&ptr->source, "test2", constructor, destructor, 1, sizeof(struct wlc_resource)));
   return true;
}

int
main(void)
{
   // TEST: Basic source and handle creation
   {
      assert(wlc_resources_init());

      struct wlc_source source;
      assert(wlc_source(&source, "test", constructor, destructor, 1, sizeof(struct wlc_resource)));

      struct wlc_resource *ptr;
      assert(!constructor_called);
      assert((ptr = wlc_handle_create(&source)));
      assert(constructor_called);
      assert(source.pool.items.count == 1);

      wlc_handle handle;
      assert(!convert_to_wlc_handle(NULL));
      assert((handle = convert_to_wlc_handle(ptr)));
      assert(!convert_from_wlc_handle(handle, "invalid"));
      assert(convert_from_wlc_handle(handle, "test") == ptr);

      const char *test = "foobar";
      wlc_handle_set_user_data(handle, test);
      assert(wlc_handle_get_user_data(handle) == test);

      assert(!destructor_called);
      wlc_handle_release(handle);
      assert(destructor_called);
      assert(!convert_from_wlc_handle(handle, "invalid"));
      assert(!convert_from_wlc_handle(handle, "test"));
      assert(!wlc_handle_get_user_data(handle));
      assert(source.pool.items.count == 0);

      wlc_source_release(&source);
      wlc_resources_terminate();
   }

   // TEST: Handle invalidation on source release
   {
      assert(wlc_resources_init());

      struct wlc_source source;
      assert(wlc_source(&source, "test", constructor, destructor, 1, sizeof(struct wlc_resource)));

      struct wlc_resource *ptr;
      assert((ptr = wlc_handle_create(&source)));
      assert(source.pool.items.count == 1);

      wlc_handle handle;
      assert((handle = convert_to_wlc_handle(ptr)));

      assert(!(destructor_called = false));
      wlc_source_release(&source);
      assert(destructor_called);

      assert(source.pool.items.count == 0);
      assert(!convert_from_wlc_handle(handle, "test"));

      wlc_resources_terminate();
   }

   // TEST: Handle invalidation on resources termination
   {
      assert(wlc_resources_init());

      struct wlc_source source;
      assert(wlc_source(&source, "test", constructor, destructor, 1, sizeof(struct wlc_resource)));

      struct wlc_resource *ptr;
      assert((ptr = wlc_handle_create(&source)));
      assert(source.pool.items.count == 1);

      wlc_handle handle;
      assert((handle = convert_to_wlc_handle(ptr)));

      assert(!(destructor_called = false));
      wlc_resources_terminate();
      assert(destructor_called);

      assert(!convert_from_wlc_handle(handle, "test"));
      wlc_source_release(&source);
   }

   // TEST: Relocation of source inside container of handle, when the handle's source changes location
   {
      assert(wlc_resources_init());

      struct wlc_source source;
      assert(wlc_source(&source, "test", constructor2, destructor2, 1, sizeof(struct contains_source)));

      struct contains_source *ptr;
      assert((ptr = wlc_handle_create(&source)));
      assert(source.pool.items.count == 1);
      void *original_source = &ptr->source;

      wlc_handle handle;
      assert((handle = convert_to_wlc_handle(ptr)));

      struct wlc_resource *ptr2;
      assert((ptr2 = wlc_handle_create(&ptr->source)));

      wlc_handle handle2;
      assert((handle2 = convert_to_wlc_handle(ptr2)));
      assert(convert_from_wlc_handle(handle2, "test2") == ptr2);

      // Play with heap until realloc does not return linear memory anymore
      {
         void *original = source.pool.items.buffer;
         do {
            void *garbage;
            assert((garbage = malloc(1024)));
            assert(wlc_handle_create(&source));
            free(garbage);
         } while (original == source.pool.items.buffer);
      }

      // So this should be true
      assert(ptr = convert_from_wlc_handle(handle, "test"));
      assert(original_source != &ptr->source);

      wlc_resources_terminate();

      assert(!convert_from_wlc_handle(handle2, "test2"));
      wlc_source_release(&source);
   }

   // TEST: Benchmark (many insertions, and removal expanding from center)
   {
      assert(wlc_resources_init());

      struct container {
         wlc_handle self;
      };

      struct wlc_source source;
      assert(wlc_source(&source, "test", constructor, destructor, 1024, sizeof(struct container)));

      wlc_handle first = 0;
      static const uint32_t iters = 0xFFFFF;
      for (uint32_t i = 0; i < iters; ++i) {
         struct container *ptr = wlc_handle_create(&source);
         ptr->self = convert_to_wlc_handle(ptr);
         if (!first) first = ptr->self;
         assert(convert_from_wlc_handle(first, "test"));
      }
      assert(source.pool.items.count == iters);

      for (uint32_t i = iters / 2, d = iters / 2; i < iters; ++i, --d) {
         assert(((struct container*)convert_from_wlc_handle(i + 1, "test"))->self == i + 1);
         assert(((struct container*)convert_from_wlc_handle(d + 1, "test"))->self == d + 1);
         wlc_handle_release(i + 1);
         wlc_handle_release(d + 1);
      }
      assert(source.pool.items.count == 0);

      assert(!convert_from_wlc_handle(first, "test"));
      wlc_source_release(&source);
      wlc_resources_terminate();
   }

   // TODO: Needs test for wlc_resource.
   //       For this we need to start compositor and some clients, or dummy the wl_resource struct.
   //       (Latter probably better)

   return EXIT_SUCCESS;
}
