#include <boost/algorithm/string.hpp>
#include <fstream>
#include <iostream>

#include "actions.h"
#include "pubsub_entry.h"

#include <yaml-cpp/yaml.h>

using goby::clang::PubSubEntry;

bool g_omit_disconnected = false;

namespace viz
{
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
            auto threads_node = interthread_node["threads"];
            for (auto it = threads_node.begin(), end = threads_node.end(); it != end; ++it)
            {
                auto thread_node = *it;
                auto thread_name = thread_node["name"].as<std::string>();
                auto bases_node = thread_node["bases"];
                std::set<std::string> bases;
                if (bases_node)
                {
                    for (auto base : bases_node) bases.insert(base.as<std::string>());
                }

                threads.emplace(thread_name,
                                std::make_shared<Thread>(thread_name, thread_node, bases));
            }

            // crosslink threads that aren't direct subclasses of goby::middleware::SimpleThread
            for (auto& thread_p : threads)
            {
                auto& bases = thread_p.second->bases;
                bool is_direct_thread_subclass = false;
                for (const auto base : bases)
                {
                    if (base.find("goby::middleware::SimpleThread") == 0)
                        is_direct_thread_subclass = true;
                }

                if (!is_direct_thread_subclass)
                {
                    for (auto& base_thread_p : threads)
                    {
                        for (const auto base : bases)
                        {
                            if (base == base_thread_p.first)
                            {
                                thread_p.second->child = base_thread_p.second;
                                base_thread_p.second->parent = thread_p.second;
                            }
                        }
                    }
                }
            }

            // after crosslinking, actually parse the yaml
            for (auto& thread_p : threads) { thread_p.second->parse_yaml(); }
        }

        auto interprocess_node = yaml["interprocess"];
        if (interprocess_node)
        {
            auto publish_node = interprocess_node["publishes"];
            for (auto p : publish_node)
                interprocess_publishes.emplace(goby::clang::Layer::INTERPROCESS, p, threads);

            auto subscribe_node = interprocess_node["subscribes"];
            for (auto s : subscribe_node)
                interprocess_subscribes.emplace(goby::clang::Layer::INTERPROCESS, s, threads);
        }
        auto intervehicle_node = yaml["intervehicle"];
        if (intervehicle_node)
        {
            auto publish_node = intervehicle_node["publishes"];
            for (auto p : publish_node)
                intervehicle_publishes.emplace(goby::clang::Layer::INTERVEHICLE, p, threads);

            auto subscribe_node = intervehicle_node["subscribes"];
            for (auto s : subscribe_node)
                intervehicle_subscribes.emplace(goby::clang::Layer::INTERVEHICLE, s, threads);
        }

        auto add_threads = [&](const std::set<PubSubEntry>& pubsubs) {
            for (const auto& e : pubsubs)
            {
                if (!threads.count(e.thread))
                    threads.emplace(e.thread, std::make_shared<Thread>(e.thread));
            }
        };

        // add main thread name for applications without interthread pub/sub
        add_threads(interprocess_publishes);
        add_threads(interprocess_subscribes);
        add_threads(intervehicle_publishes);
        add_threads(intervehicle_subscribes);
    }

    std::string name;
    std::map<std::string, std::shared_ptr<Thread>> threads;
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

const auto vehicle_color = "darkgreen";
const auto process_color = "dodgerblue4";
const auto thread_color = "purple4";

std::string node_name(std::string p, std::string a, std::string th)
{
    std::vector<char> reserved{{':', '&', '<', '>', ' ', ','}};

    for (char r : reserved)
    {
        std::string search(1, r);
        auto replacement = "_" + std::to_string(static_cast<int>(r)) + "_";
        boost::replace_all(p, search, replacement);
        boost::replace_all(a, search, replacement);
        boost::replace_all(th, search, replacement);
    }

    return p + "_" + a + "_" + th;
}

std::string connection_with_label_final(const PubSubEntry& pub, std::string pub_str,
                                        std::string sub_str, std::string color)
{
    return pub_str + "->" + sub_str + "[label=<<b><font point-size=\"10\">" + pub.group +
           "</font></b><br/><font point-size=\"6\">" + pub.scheme +
           "</font><br/><font point-size=\"8\">" + pub.type + "</font>>" + "color=" + color + "]\n";
}

std::string connection_with_label(std::string pub_platform, std::string pub_application,
                                  const PubSubEntry& pub, std::string sub_platform,
                                  std::string sub_application, const PubSubEntry& sub,
                                  std::string color)
{
    return connection_with_label_final(pub, node_name(pub_platform, pub_application, pub.thread),
                                       node_name(sub_platform, sub_application, sub.thread), color);
}

std::string disconnected_publication(std::string pub_platform, std::string pub_application,
                                     const PubSubEntry& pub, std::string color)
{
    if(g_omit_disconnected)
        return "";
    
    // hide inner publications without subscribers.
    if (pub.is_inner_pub)
        return "";
    else
        return node_name(pub_platform, pub_application, pub.thread) + "_no_subscribers_" + color +
               " [label=\"\",style=invis] \n" +
               connection_with_label_final(pub,
                                           node_name(pub_platform, pub_application, pub.thread),
                                           node_name(pub_platform, pub_application, pub.thread) +
                                               "_no_subscribers_" + color,
                                           color);
}

std::string disconnected_subscription(std::string sub_platform, std::string sub_application,
                                      const PubSubEntry& sub, std::string color)
{
    if(g_omit_disconnected)
        return "";

    return node_name(sub_platform, sub_application, sub.thread) + "_no_publishers_" + color +
           " [label=\"\",style=invis] \n" +
           connection_with_label_final(sub,
                                       node_name(sub_platform, sub_application, sub.thread) +
                                           "_no_publishers_" + color,
                                       node_name(sub_platform, sub_application, sub.thread), color);
}

void write_thread_connections(std::ofstream& ofs, const viz::Platform& platform,
                              const viz::Application& application, const viz::Thread& thread,
                              std::set<PubSubEntry>& disconnected_subs)
{
    std::set<PubSubEntry> disconnected_pubs;
    for (const auto& pub : thread.interthread_publishes)
    {
        disconnected_pubs.insert(pub);

        for (const auto& sub_thread_p : application.threads)
        {
            const auto& sub_thread = sub_thread_p.second;
            for (const auto& sub : sub_thread->interthread_subscribes)
            {
                if (goby::clang::connects(pub, sub))
                {
                    remove_disconnected(pub, sub, disconnected_pubs, disconnected_subs);
                    ofs << "\t\t\t"
                        << connection_with_label(platform.name, application.name, pub,
                                                 platform.name, application.name, sub, thread_color)
                        << "\n";
                }
            }
        }
    }

    for (const auto& pub : disconnected_pubs)
        ofs << "\t\t\t"
            << disconnected_publication(platform.name, application.name, pub, thread_color) << "\n";
}

void write_process_connections(std::ofstream& ofs, const viz::Platform& platform,
                               const viz::Application& pub_application,
                               std::map<std::string, std::set<PubSubEntry>>& disconnected_subs)
{
    std::set<PubSubEntry> disconnected_pubs;

    for (const auto& pub : pub_application.interprocess_publishes)
    {
        disconnected_pubs.insert(pub);
        for (const auto& sub_application : platform.applications)
        {
            for (const auto& sub : sub_application.interprocess_subscribes)
            {
                if (goby::clang::connects(pub, sub))
                {
                    remove_disconnected(pub, sub, disconnected_pubs,
                                        disconnected_subs[sub_application.name]);

                    ofs << "\t\t"
                        << connection_with_label(platform.name, pub_application.name, pub,
                                                 platform.name, sub_application.name, sub,
                                                 process_color)
                        << "\n";
                }
            }
        }
    }

    for (const auto& pub : disconnected_pubs)
        ofs << "\t\t\t"
            << disconnected_publication(platform.name, pub_application.name, pub, process_color)
            << "\n";
}

void write_vehicle_connections(
    std::ofstream& ofs, const viz::Deployment& deployment, const viz::Platform& pub_platform,
    const viz::Application& pub_application,
    std::map<std::string, std::map<std::string, std::set<PubSubEntry>>>& disconnected_subs)
{
    std::set<PubSubEntry> disconnected_pubs;
    for (const auto& pub : pub_application.intervehicle_publishes)
    {
        disconnected_pubs.insert(pub);
        for (const auto& sub_platform : deployment.platforms)
        {
            for (const auto& sub_application : sub_platform.applications)
            {
                for (const auto& sub : sub_application.intervehicle_subscribes)
                {
                    if (goby::clang::connects(pub, sub))
                    {
                        remove_disconnected(
                            pub, sub, disconnected_pubs,
                            disconnected_subs[sub_platform.name][sub_application.name]);

                        ofs << "\t\t"
                            << connection_with_label(pub_platform.name, pub_application.name, pub,
                                                     sub_platform.name, sub_application.name, sub,
                                                     vehicle_color)
                            << "\n";
                    }
                }
            }
        }
    }

    for (const auto& pub : disconnected_pubs)
        ofs << "\t\t\t"
            << disconnected_publication(pub_platform.name, pub_application.name, pub, vehicle_color)
            << "\n";
}

int goby::clang::visualize(const std::vector<std::string>& yamls, std::string output_directory,
                           std::string output_file, std::string deployment_config_input, bool omit_disconnected)
{
    g_omit_disconnected = omit_disconnected;
    
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

    if (output_file.empty())
        output_file = deployment.name + ".dot";

    std::string file_name(output_directory + "/" + output_file);
    std::ofstream ofs(file_name.c_str());
    if (!ofs.is_open())
    {
        std::cerr << "Failed to open " << file_name << " for writing" << std::endl;
        exit(EXIT_FAILURE);
    }

    int cluster = 0;
    ofs << "digraph " << deployment.name << " { \n";
    ofs << " splines=polyline\n";
    

    std::map<std::string, std::map<std::string, std::set<PubSubEntry>>> platform_disconnected_subs;
    for (const auto& sub_platform : deployment.platforms)
    {
        for (const auto& sub_application : sub_platform.applications)
        {
            for (const auto& sub : sub_application.intervehicle_subscribes)
            { platform_disconnected_subs[sub_platform.name][sub_application.name].insert(sub); } }
    }

    for (const auto& platform : deployment.platforms)
    {
        ofs << "\tsubgraph cluster_" << cluster++ << " {\n";
        ofs << "\tlabel=\"" << platform.name << "\"\n";
        ofs << "\tfontcolor=\"" << vehicle_color << "\"\n";

        std::map<std::string, std::set<PubSubEntry>> process_disconnected_subs;
        for (const auto& application : platform.applications)
        {
            for (const auto& sub : application.interprocess_subscribes)
                process_disconnected_subs[application.name].insert(sub);
        }

        for (const auto& application : platform.applications)
        {
            ofs << "\t\tsubgraph cluster_" << cluster++ << " {\n";
            ofs << "\t\tlabel=\"" << application.name << "\"\n";
            ofs << "\t\tfontcolor=\"" << process_color << "\"\n";

            std::set<PubSubEntry> thread_disconnected_subs;
            for (const auto& thread_p : application.threads)
            {
                const auto& thread = thread_p.second;

                for (const auto& sub : thread->interthread_subscribes)
                    thread_disconnected_subs.insert(sub);
            }

            for (const auto& thread_p : application.threads)
            {
                const auto& thread = thread_p.second;

                std::string thread_display_name = thread->most_derived_name();
                using boost::algorithm::replace_all;
                replace_all(thread_display_name, "&", "&amp;");
                replace_all(thread_display_name, "\"", "&quot;");
                replace_all(thread_display_name, "\'", "&apos;");
                replace_all(thread_display_name, "<", "&lt;");
                replace_all(thread_display_name, ">", "&gt;");

                boost::algorithm::replace_first(thread_display_name, "&lt;", "<br/>&lt;");
                replace_all(thread_display_name, "&lt;", "<font point-size=\"10\">&lt;");
                replace_all(thread_display_name, "&gt;", "&gt;</font>");
                
                replace_all(thread_display_name, ", ", ",<br/>");

                ofs << "\t\t\t"
                    << node_name(platform.name, application.name, thread->most_derived_name())
                    << " [label=<" << thread_display_name << ">,fontcolor=" << thread_color
                    << ",shape=box]\n";

                write_thread_connections(ofs, platform, application, *thread,
                                         thread_disconnected_subs);
            }

            for (const auto& sub : thread_disconnected_subs)
            {
                ofs << "\t\t\t"
                    << disconnected_subscription(platform.name, application.name, sub, thread_color)
                    << "\n";
            }

            ofs << "\t\t}\n";

            write_process_connections(ofs, platform, application, process_disconnected_subs);
        }

        for (const auto& sub_p : process_disconnected_subs)
        {
            for (const auto& sub : sub_p.second)
            {
                ofs << "\t\t\t"
                    << disconnected_subscription(platform.name, sub_p.first, sub, process_color)
                    << "\n";
            }
        }

        ofs << "\t}\n";

        for (const auto& application : platform.applications)
            write_vehicle_connections(ofs, deployment, platform, application,
                                      platform_disconnected_subs);
    }

    for (const auto& sub_plat_p : platform_disconnected_subs)
    {
        for (const auto& sub_app_p : sub_plat_p.second)
        {
            for (const auto& sub : sub_app_p.second)
            {
                ofs << "\t\t\t"
                    << disconnected_subscription(sub_plat_p.first, sub_app_p.first, sub,
                                                 vehicle_color)
                    << "\n";
            }
        }
    }

    ofs << "}\n";

    return 0;
}
