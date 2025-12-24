// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2025 Otitis Media
#pragma once

#include <juce_core/juce_core.h>
#include <unordered_map>
#include "Logging.h"

#ifndef MDW_VERBOSE_CACHE_LOGS
#define MDW_VERBOSE_CACHE_LOGS 0
#endif

namespace milkdawp {

// Phase 7.1: Minimal shared cache across instances (process-global within host)
// Caches lightweight preset metadata to avoid redundant disk/parse work.
class SharedAssetCache {
public:
    struct PresetMeta {
        juce::String name;        // Display name
        int paletteIndex = 0;     // Derived palette index used by the stub
        juce::Time lastModified;  // File timestamp when computed
        int refCount = 0;         // Number of active users across instances
    };

    static SharedAssetCache& instance()
    {
        static SharedAssetCache inst;
        return inst;
    }

    // Returns true on cache hit and fills out.
    bool getPresetMeta(const juce::String& fullPath, PresetMeta& out)
    {
        const juce::ScopedLock sl(lock);
        if (map.contains(fullPath)) { out = map[fullPath]; return true; }
        return false;
    }

    // Update or insert meta without touching refcount.
    void upsertPresetMeta(const juce::String& fullPath, const PresetMeta& meta)
    {
        const juce::ScopedLock sl(lock);
        map.set(fullPath, meta);
    }

    void addRef(const juce::String& fullPath)
    {
        const juce::ScopedLock sl(lock);
        if (!map.contains(fullPath)) {
            PresetMeta m; m.name = juce::File(fullPath).getFileNameWithoutExtension();
            m.lastModified = juce::File(fullPath).getLastModificationTime();
            m.paletteIndex = derivePaletteIndex(m.name);
            m.refCount = 1;
            map.set(fullPath, m);
            if (MDW_VERBOSE_CACHE_LOGS) {
                MDW_LOG_INFO(juce::String("Cache addRef: ") + fullPath + " rc=1 (new)");
            }
        } else {
            auto m = map[fullPath];
            m.refCount++;
            map.set(fullPath, m);
            if (MDW_VERBOSE_CACHE_LOGS) {
                MDW_LOG_INFO(juce::String("Cache addRef: ") + fullPath + " rc=" + juce::String(m.refCount));
            }
        }
    }

    void release(const juce::String& fullPath)
    {
        const juce::ScopedLock sl(lock);
        if (map.contains(fullPath)) {
            auto m = map[fullPath];
            m.refCount--;
            if (m.refCount <= 0) {
                map.remove(fullPath);
                if (MDW_VERBOSE_CACHE_LOGS) {
                    MDW_LOG_INFO(juce::String("Cache release: ") + fullPath + " rc=0 (evict)");
                }
            } else {
                map.set(fullPath, m);
                if (MDW_VERBOSE_CACHE_LOGS) {
                    MDW_LOG_INFO(juce::String("Cache release: ") + fullPath + " rc=" + juce::String(m.refCount));
                }
            }
        }
    }

    // Utility: compute same palette index rule as VisualizationThread stub
    static int derivePaletteIndex(const juce::String& presetName)
    {
        const int hash = presetName.hashCode();
        const int palettes = 5;
        return (hash == juce::String().hashCode()) ? 0 : (std::abs(hash) % palettes);
    }

private:
    SharedAssetCache() = default;

    juce::CriticalSection lock;
    juce::HashMap<juce::String, PresetMeta> map; // key: full path
};

} // namespace milkdawp
