#ifndef PUBSUB_ENTRY_20190801H
#define PUBSUB_ENTRY_20190801H

#include <string>

#include "yaml_raii.h"

namespace goby
{
namespace clang
{
enum class Layer
{
    UNKNOWN = -1,
    INTERTHREAD = 0,
    INTERPROCESS = 1,
    INTERVEHICLE = 2
};

struct PubSubEntry
{
    PubSubEntry(Layer l, const YAML::Node& yaml, const std::string& th = "")
    {
        layer = l;
        auto thread_node = yaml["thread"];
        if (thread_node)
            thread = thread_node.as<std::string>();
        else
            thread = th;

        group = yaml["group"].as<std::string>();
        scheme = yaml["scheme"].as<std::string>();
        type = yaml["type"].as<std::string>();
    }

    PubSubEntry(Layer l, std::string th, std::string g, std::string s, std::string t)
        : layer(l), thread(th), group(g), scheme(s), type(t)
    {
    }

    Layer layer{Layer::UNKNOWN};
    std::string thread;
    std::string group;
    std::string scheme;
    std::string type;

    void write_yaml_map(YAML::Emitter& yaml_out, bool include_thread = true) const
    {
        goby::yaml::YMap entry_map(yaml_out, false);
        entry_map.add("group", group);
        entry_map.add("scheme", scheme);
        entry_map.add("type", type);
        if (include_thread)
            entry_map.add("thread", thread);
    }
};

inline std::ostream& operator<<(std::ostream& os, const PubSubEntry& e)
{
    return os << "layer: " << static_cast<int>(e.layer) << ", thread: " << e.thread
              << ", group: " << e.group << ", scheme: " << e.scheme << ", type: " << e.type;
}

inline bool operator<(const PubSubEntry& a, const PubSubEntry& b)
{
    if (a.layer != b.layer)
        return a.layer < b.layer;
    if (a.thread != b.thread)
        return a.thread < b.thread;
    if (a.group != b.group)
        return a.group < b.group;
    if (a.scheme != b.scheme)
        return a.scheme < b.scheme;

    return a.type < b.type;
}

inline bool connects(const PubSubEntry& a, const PubSubEntry& b)
{
    return a.layer == b.layer && a.group == b.group &&
           (a.scheme == b.scheme || a.scheme == "CXX_OBJECT" || b.scheme == "CXX_OBJECT") &&
           a.type == b.type;
}

} // namespace clang
} // namespace goby

#endif
