#include "core/ResultTree.h"

namespace digga
{

namespace
{
    juce::String joinPath (const std::vector<int>& path, const char* separator)
    {
        juce::StringArray parts;
        for (int p : path)
            parts.add (juce::String (p));
        return parts.joinIntoString (separator);
    }
}

juce::String ResultNode::displayName() const
{
    const juce::String dash (juce::CharPointer_UTF8 (" \xe2\x80\x93 "));
    const auto base = kind == ClipKind::loop ? slot : "Shot " + slot;

    if (path.empty())
        return kind == ClipKind::loop ? base + dash + "Loop " + juce::String (bars) + " bars" : base;

    // the one-shot column is narrow: variations there drop the prefix
    if (kind == ClipKind::shot)
        return "Kill Mix " + joinPath (path, ".");

    return base + dash + "Kill Mix " + joinPath (path, ".");
}

juce::String ResultNode::fileTag() const
{
    const auto base = kind == ClipKind::loop ? slot : "Shot" + slot;
    return path.empty() ? base : base + "_KillMix" + joinPath (path, "-");
}

juce::String ResultNode::key() const
{
    return (kind == ClipKind::loop ? slot : "S" + slot) + ":" + joinPath (path, ".");
}

double ResultNode::lengthSeconds (double sampleRate) const
{
    return audio != nullptr && sampleRate > 0.0 ? audio->getNumSamples() / sampleRate : 0.0;
}

int ResultTree::add (ResultNode node)
{
    node.id = nextId++;
    nodes.push_back (std::move (node));
    return nodes.back().id;
}

const ResultNode* ResultTree::find (int id) const
{
    for (const auto& n : nodes)
        if (n.id == id)
            return &n;
    return nullptr;
}

ResultNode* ResultTree::find (int id)
{
    for (auto& n : nodes)
        if (n.id == id)
            return &n;
    return nullptr;
}

const ResultNode* ResultTree::findByKey (const juce::String& key) const
{
    for (const auto& n : nodes)
        if (n.key() == key)
            return &n;
    return nullptr;
}

std::vector<int> ResultTree::childrenOf (int id) const
{
    std::vector<int> ids;
    for (const auto& n : nodes)
        if (n.parentId == id)
            ids.push_back (n.id);
    return ids;
}

void ResultTree::removeDescendants (int id)
{
    for (int child : childrenOf (id))
    {
        removeDescendants (child);
        nodes.erase (std::remove_if (nodes.begin(), nodes.end(), [child] (const ResultNode& n) { return n.id == child; }),
                     nodes.end());
    }
}

void ResultTree::appendRows (int id, int depth, bool last, std::vector<Row>& rows) const
{
    const auto* node = find (id);
    const auto children = childrenOf (id);
    rows.push_back ({ node, depth, last, ! children.empty() });

    if (node->expanded)
        for (size_t i = 0; i < children.size(); ++i)
            appendRows (children[i], depth + 1, i + 1 == children.size(), rows);
}

std::vector<ResultTree::Row> ResultTree::visibleRows (ClipKind kind) const
{
    std::vector<Row> rows;
    for (const auto& n : nodes)
        if (n.isRoot() && n.kind == kind)
            appendRows (n.id, 0, false, rows);
    return rows;
}

} // namespace digga
