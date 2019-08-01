#include <fstream>
#include <iostream>

#include "actions.h"
#include "pubsub_entry.h"

#include <yaml-cpp/yaml.h>

using goby::clang::PubSubEntry;

namespace viz
{
struct Thread
{
    Thread(std::string n) : name(n) {}

    Thread(std::string n, const YAML::Node& yaml) : name(n)
    {
        auto publish_node = yaml["publishes"];
        for (auto p : publish_node)
            interthread_publishes.emplace(goby::clang::Layer::INTERTHREAD, p, n);

        auto subscribe_node = yaml["subscribes"];
        for (auto s : subscribe_node)
            interthread_subscribes.emplace(goby::clang::Layer::INTERTHREAD, s, n);
    }

    std::string name;
    std::set<PubSubEntry> interthread_publishes;
    std::set<PubSubEntry> interthread_subscribes;
};

inline bool operator<(const Thread& a, const Thread& b) { return a.name < b.name; }

inline std::ostream& operator<<(std::ostream& os, const Thread& th)
{
    os << th.name << " | ";
    if (!th.interthread_publishes.empty() || !th.interthread_subscribes.empty())
    {
        using goby::clang::operator<<;
        for (const auto& p : th.interthread_publishes) os << "[PUB " << p << "]";
        for (const auto& s : th.interthread_subscribes) os << "[SUB " << s << "]";
    }
    return os;
}

struct Application
{
    // create from root interface.yml node
    Application(const YAML::Node& yaml)
    {
        name = yaml["application"].as<std::string>();

        auto interthread_node = yaml["interthread"];
        if (interthread_node)
        {
            for (auto it = interthread_node.begin(), end = interthread_node.end(); it != end; ++it)
                threads.emplace(it->first.as<std::string>(),
                                Thread(it->first.as<std::string>(), it->second));
        }

        auto interprocess_node = yaml["interprocess"];
        if (interprocess_node)
        {
            auto publish_node = interprocess_node["publishes"];
            for (auto p : publish_node)
                interprocess_publishes.emplace(goby::clang::Layer::INTERPROCESS, p);

            auto subscribe_node = interprocess_node["subscribes"];
            for (auto s : subscribe_node)
                interprocess_subscribes.emplace(goby::clang::Layer::INTERPROCESS, s);
        }
        auto intervehicle_node = yaml["intervehicle"];
        if (intervehicle_node)
        {
            auto publish_node = intervehicle_node["publishes"];
            for (auto p : publish_node)
                intervehicle_publishes.emplace(goby::clang::Layer::INTERVEHICLE, p);

            auto subscribe_node = intervehicle_node["subscribes"];
            for (auto s : subscribe_node)
                intervehicle_subscribes.emplace(goby::clang::Layer::INTERVEHICLE, s);
        }

        auto add_threads = [&](const std::set<PubSubEntry>& pubsubs) {
            for (const auto& e : pubsubs)
            {
                if (!threads.count(e.thread))
                    threads.emplace(e.thread, Thread(e.thread));
            }
        };

        // add main thread name for applications without interthread pub/sub
        add_threads(interprocess_publishes);
        add_threads(interprocess_subscribes);
        add_threads(intervehicle_publishes);
        add_threads(intervehicle_subscribes);
    }

    std::string name;
    std::map<std::string, Thread> threads;
    std::set<PubSubEntry> interprocess_publishes;
    std::set<PubSubEntry> interprocess_subscribes;
    std::set<PubSubEntry> intervehicle_publishes;
    std::set<PubSubEntry> intervehicle_subscribes;
};

inline bool operator<(const Application& a, const Application& b) { return a.name < b.name; }

inline std::ostream& operator<<(std::ostream& os, const Application& a)
{
    os << a.name << " | ";
    os << "intervehicle: ";
    if (!a.intervehicle_publishes.empty() || !a.intervehicle_subscribes.empty())
    {
        using goby::clang::operator<<;
        for (const auto& p : a.intervehicle_publishes) os << "[PUB " << p << "]";
        for (const auto& s : a.intervehicle_subscribes) os << "[SUB " << s << "]";
    }
    else
    {
        os << "NONE";
    }
    os << " | interprocess: ";
    if (!a.interprocess_publishes.empty() || !a.interprocess_subscribes.empty())
    {
        using goby::clang::operator<<;
        for (const auto& p : a.interprocess_publishes) os << "[PUB " << p << "]";
        for (const auto& s : a.interprocess_subscribes) os << "[SUB " << s << "]";
    }
    else
    {
        os << "NONE";
    }

    if (!a.threads.empty())
    {
        os << " | ";
        os << "interthread: ";
        for (const auto& th_p : a.threads) os << "{" << th_p.second << "}";
    }

    return os;
}

struct Platform
{
    Platform(const std::string& n, const std::vector<std::string>& yamls) : name(n)
    {
        // each yaml represents a given application
        for (const auto& yaml_file : yamls)
        {
            YAML::Node yaml;
            try
            {
                yaml = YAML::LoadFile(yaml_file);
            }
            catch (const std::exception& e)
            {
                std::cout << "Failed to parse " << yaml_file << ": " << e.what() << std::endl;
            }

            applications.emplace(yaml);
        }
    }

    std::string name;
    std::set<Application> applications;
};

inline bool operator<(const Platform& a, const Platform& b) { return a.name < b.name; }

inline std::ostream& operator<<(std::ostream& os, const Platform& p)
{
    os << "((" << p.name << "))" << std::endl;
    for (const auto& a : p.applications) os << "Application: " << a << std::endl;
    return os;
}

struct Deployment
{
    Deployment(const std::string& n,
               const std::map<std::string, std::vector<std::string>>& platform_yamls)
        : name(n)
    {
        for (const auto& platform_yaml_p : platform_yamls)
        { platforms.emplace(platform_yaml_p.first, platform_yaml_p.second); } }

    std::string name;
    std::set<Platform> platforms;
};

inline std::ostream& operator<<(std::ostream& os, const Deployment& d)
{
    os << "-----" << d.name << "-----" << std::endl;
    for (const auto& p : d.platforms) os << "Platform: " << p << std::endl;
    return os;
}

} // namespace viz

int goby::clang::visualize(const std::vector<std::string>& yamls, std::string output_directory,
                           std::string deployment_config_input)
{
    std::string deployment_name;

    // maps platform name to yaml files
    std::map<std::string, std::vector<std::string>> platform_yamls;

    // assume deployment file
    if (deployment_config_input.empty())
    {
        YAML::Node deploy_yaml;
        try
        {
            deploy_yaml = YAML::LoadFile(yamls.at(0));
        }
        catch (const std::exception& e)
        {
            std::cout << "Failed to parse deployment file: " << deployment_config_input << ": "
                      << e.what() << std::endl;
        }
        deployment_name = deploy_yaml["deployment"].as<std::string>();
        YAML::Node platforms_node = deploy_yaml["platforms"];
        if (!platforms_node || !platforms_node.IsSequence())
        {
            std::cerr << "Must specify platforms: as a sequence in deployment YAML file"
                      << std::endl;
            exit(EXIT_FAILURE);
        }

        for (auto platform : platforms_node)
        {
            std::string name = platform["name"].as<std::string>();
            YAML::Node interfaces_node = platform["interfaces"];

            if (!interfaces_node || !interfaces_node.IsSequence())
            {
                std::cerr << "Must specify interfaces: as a sequence in deployment YAML file for "
                             "platform: "
                          << name << std::endl;
                exit(EXIT_FAILURE);
            }

            for (auto interface_yaml : interfaces_node)
                platform_yamls[name].push_back(interface_yaml.as<std::string>());
        }
    }
    else
    {
        // degenerate case without deployment yml, e.g.
        // goby_clang_tool -viz -deployment example goby3_example_basic_interprocess_publisher_interface.yml goby3_example_basic_interprocess_subscriber_interface.yml

        // "deployment_config_input" is the name, not a file
        deployment_name = deployment_config_input;
        // use the yaml files passed as arguments to goby_clang_tool as if they belong to one deployment
        platform_yamls.insert(std::make_pair("default", yamls));
    }

    viz::Deployment deployment(deployment_name, platform_yamls);

    std::string file_name(output_directory + "/" + deployment.name + ".dot");
    std::ofstream ofs(file_name.c_str());
    if (!ofs.is_open())
    {
        std::cerr << "Failed to open " << file_name << " for writing" << std::endl;
        exit(EXIT_FAILURE);
    }

    std::cout << deployment << std::endl;

    auto node_name = [](std::string p, std::string a, std::string th) -> std::string {
        return p + "_" + a + "_" + th;
    };

    auto write_connection = [&node_name](std::string p, std::string a, const PubSubEntry& pub,
                                   const PubSubEntry& sub) -> std::string {
        return node_name(p, a, pub.thread) + "->" + node_name(p, a, sub.thread) + "[label=<" +
               pub.group + "<br/>" + pub.scheme + "<br/>" + pub.type + ">]\n";
    };

    int cluster = 0;
    ofs << "digraph " << deployment.name << " { \n";
    for (const auto& platform : deployment.platforms)
    {
        ofs << "\tsubgraph cluster_" << cluster++ << " {\n";
        ofs << "\tlabel=\"" << platform.name << "\"\n";
        for (const auto& application : platform.applications)
        {
            ofs << "\t\tlabel=\"" << application.name << "\"\n";
            ofs << "\t\tsubgraph cluster_" << cluster++ << " {\n";
            for (const auto& thread_p : application.threads)
            {
                const auto& thread = thread_p.second;

                ofs << "\t\t\t" << node_name(platform.name, application.name, thread.name)
                    << " [label=\"" << thread.name << "\"]\n";

                for (const auto& pub : thread.interthread_publishes)
                {
                    for (const auto& inner_thread_p : application.threads)
                    {
                        const auto& inner_thread = inner_thread_p.second;
                        for (const auto& sub : inner_thread.interthread_subscribes)
                        {
                            if (goby::clang::connects(pub, sub))
                            {
                                ofs << "\t\t\t"
                                    << write_connection(platform.name, application.name, pub, sub)
                                    << "\n";
                            }
                        }
                    }
                }
            }
            ofs << "\t\t}\n";
        }
        ofs << "\t}\n";
    }

    ofs << "}\n";

    return 0;
}
