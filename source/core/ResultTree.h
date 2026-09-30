#pragma once

#include "core/AudioTools.h"

#include <juce_data_structures/juce_data_structures.h>

#include <vector>

namespace digga
{

enum class ClipKind { loop, shot };

/** One generated result: a loop, a one-shot, or a KILL variation of either. */
struct ResultNode
{
    int id = 0;
    int parentId = -1;
    ClipKind kind = ClipKind::loop;
    juce::String slot;            // "A1".."B2" for loops, "1".."8" for shots
    int bars = 0;                 // loops only
    std::vector<int> path;        // KILL indices from the root, e.g. {3, 2}
    juce::uint32 seed = 0;        // KILL seed (variations only)
    float strength = 0.0f;        // KILL strength used
    AudioPtr audio;
    std::vector<float> peaks;
    bool expanded = true;

    bool isRoot() const noexcept { return parentId < 0; }
    juce::String displayName() const;   // "A2 – Kill Mix 3.2", "Shot 4"
    juce::String fileTag() const;       // "A2_KillMix3-2", "Shot4"
    juce::String key() const;           // stable id for saved state: "A2:3.2"
    double lengthSeconds (double sampleRate) const;
};

/** Parent / child structure of all results. Cheap to copy (audio is shared),
    which is how undo snapshots are taken. */
class ResultTree
{
public:
    void clear() { nodes.clear(); }
    bool isEmpty() const noexcept { return nodes.empty(); }

    int add (ResultNode node);                  // returns the new id
    const ResultNode* find (int id) const;
    ResultNode* find (int id);
    const ResultNode* findByKey (const juce::String& key) const;

    std::vector<int> childrenOf (int id) const;
    void removeDescendants (int id);

    struct Row
    {
        const ResultNode* node;
        int depth;
        bool lastSibling;
        bool hasChildren;
    };

    /** Depth-first list of what the tracklist shows (collapsed subtrees hidden). */
    std::vector<Row> visibleRows (ClipKind kind) const;

    const std::vector<ResultNode>& all() const noexcept { return nodes; }

private:
    void appendRows (int id, int depth, bool last, std::vector<Row>& rows) const;

    std::vector<ResultNode> nodes;
    int nextId = 1;
};

} // namespace digga
