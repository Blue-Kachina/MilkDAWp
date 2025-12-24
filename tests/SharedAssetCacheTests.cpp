#include <juce_core/juce_core.h>
#include "src/SharedAssetCache.h"

using namespace milkdawp;

class SharedAssetCacheTests : public juce::UnitTest {
public:
    SharedAssetCacheTests() : juce::UnitTest("SharedAssetCacheTests", "core") {}

    void runTest() override
    {
        beginTest("Hit/miss and upsert lifecycle");
        {
            auto& cache = SharedAssetCache::instance();
            const juce::String path = 
#if JUCE_WINDOWS
                "C:/tmp/fake_preset_path_unit_test.milk";
#else
                "/tmp/fake_preset_path_unit_test.milk";
#endif
            SharedAssetCache::PresetMeta meta{};
            expect(!cache.getPresetMeta(path, meta), "Cache unexpectedly contained entry before insert");

            SharedAssetCache::PresetMeta m0{};
            m0.name = "UnitTestPreset";
            m0.paletteIndex = SharedAssetCache::derivePaletteIndex(m0.name);
            m0.lastModified = juce::Time::getCurrentTime();
            cache.upsertPresetMeta(path, m0);

            SharedAssetCache::PresetMeta m1{};
            expect(cache.getPresetMeta(path, m1), "Cache should return hit after upsert");
            expectEquals(m1.name, m0.name);
            expectEquals(m1.paletteIndex, m0.paletteIndex);
            expect(m1.lastModified.toMilliseconds() == m0.lastModified.toMilliseconds());
        }

        beginTest("Refcount add/release and eviction");
        {
            auto& cache = SharedAssetCache::instance();
            const juce::String path = 
#if JUCE_WINDOWS
                "C:/tmp/fake_preset_path_unit_test_rc.milk";
#else
                "/tmp/fake_preset_path_unit_test_rc.milk";
#endif
            // Ensure clean slate: if an old entry exists, release it until evicted
            SharedAssetCache::PresetMeta tmp{};
            if (cache.getPresetMeta(path, tmp)) {
                // Drain refcount by releasing until removed (defensive; normally not needed)
                for (int i = 0; i < 8; ++i) cache.release(path);
            }

            // addRef should create a new entry with rc=1 if missing
            cache.addRef(path);
            SharedAssetCache::PresetMeta m{};
            expect(cache.getPresetMeta(path, m), "Entry should exist after first addRef");
            expect(m.refCount >= 1, "Refcount should be >= 1 after addRef");

            // addRef again increments
            cache.addRef(path);
            SharedAssetCache::PresetMeta m2{};
            expect(cache.getPresetMeta(path, m2), "Entry should still exist after second addRef");
            expect(m2.refCount >= 2, "Refcount should be >= 2 after second addRef");

            // release once -> rc should drop but entry remain
            cache.release(path);
            SharedAssetCache::PresetMeta m3{};
            expect(cache.getPresetMeta(path, m3), "Entry should remain after first release");
            expect(m3.refCount >= 1, "Refcount should be >= 1 after first release");

            // release again -> should evict when rc reaches 0
            cache.release(path);
            SharedAssetCache::PresetMeta m4{};
            expect(!cache.getPresetMeta(path, m4), "Entry should be evicted at rc=0");
        }

        beginTest("Palette derivation is deterministic");
        {
            const juce::String name = "UnitTestPresetABC";
            const int p1 = SharedAssetCache::derivePaletteIndex(name);
            const int p2 = SharedAssetCache::derivePaletteIndex(name);
            expectEquals(p1, p2);
        }
    }
};

static SharedAssetCacheTests sharedAssetCacheTests;
