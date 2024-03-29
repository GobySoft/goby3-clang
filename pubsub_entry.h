#ifndef PUBSUB_ENTRY_20190801H
#define PUBSUB_ENTRY_20190801H

#include <string>

#include "yaml_raii.h"

namespace viz
{
class Thread;
}

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
    PubSubEntry(Layer l, const YAML::Node& yaml,
                const std::map<std::string, std::shared_ptr<viz::Thread>> threads);

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
        auto inner_node = yaml["inner"];
        if (inner_node && inner_node.as<bool>())
            is_inner_pub = true;
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

    void write_yaml_map(YAML::Emitter& yaml_out, bool include_thread = true,
                        bool inner_pub = false) const
    {
        goby::yaml::YMap entry_map(yaml_out, false);
        entry_map.add("group", group);
        entry_map.add("scheme", scheme);
        entry_map.add("type", type);
        if (include_thread)
            entry_map.add("thread", thread);

        // publication was automatically added to this scope from an outer publisher
        if (inner_pub)
            entry_map.add("inner", "true");
    }

    bool is_inner_pub{false};
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

inline void remove_disconnected(PubSubEntry pub, PubSubEntry sub,
                                std::set<PubSubEntry>& disconnected_pubs,
                                std::set<PubSubEntry>& disconnected_subs)
{
    disconnected_pubs.erase(pub);
    disconnected_subs.erase(sub);

    auto cxx_sub = sub;
    cxx_sub.scheme = "CXX_OBJECT";
    disconnected_subs.erase(cxx_sub);
}

} // namespace clang
} // namespace goby

namespace viz
{
struct Thread
{
    Thread(std::string n, std::set<std::string> b = std::set<std::string>()) : name(n), bases(b) {}
    Thread(std::string n, const YAML::Node& y, std::set<std::string> b = std::set<std::string>())
        : name(n), bases(b), yaml(y)
    {
    }

    void parse_yaml()
    {
        auto publish_node = yaml["publishes"];
        for (auto p : publish_node)
            interthread_publishes.emplace(goby::clang::Layer::INTERTHREAD, p, most_derived_name());

        auto subscribe_node = yaml["subscribes"];
        for (auto s : subscribe_node)
            interthread_subscribes.emplace(goby::clang::Layer::INTERTHREAD, s, most_derived_name());
    }

    std::string most_derived_name()
    {
        if (parent)
            return parent->most_derived_name();
        else
            return name;
    }

    std::string name;
    std::set<std::string> bases;
    YAML::Node yaml;

    // child thread instance if we're not a direct base of SimpleThread
    std::shared_ptr<Thread> child;
    // this thread has a parent who isn't a direct base of SimpleThread
    std::shared_ptr<Thread> parent;

    std::set<goby::clang::PubSubEntry> interthread_publishes;
    std::set<goby::clang::PubSubEntry> interthread_subscribes;
};
} // namespace viz

inline goby::clang::PubSubEntry::PubSubEntry(
    Layer l, const YAML::Node& yaml,
    const std::map<std::string, std::shared_ptr<viz::Thread>> threads)
    : PubSubEntry(l, yaml)
{
    if (threads.count(thread))
        thread = threads.at(thread)->most_derived_name();
}

#endif
