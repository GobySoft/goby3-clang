
namespace goby
{
namespace middleware
{
class Group
{
  public:
    constexpr Group(const char* c) : c_(c) {}
    constexpr const char* c_str() const { return c_; }

  private:
    const char* c_{nullptr};
};

class StaticTransporterInterface
{
  public:
    template <const Group& group, typename Data, int scheme = 42> void publish(const Data& data) {}

    template <const Group& group, typename Data, int scheme = 42> void subscribe() {}
    
    void baz() {}
};

class InterProcessTransporter : public StaticTransporterInterface
{
};

} // namespace middleware
} // namespace goby

constexpr goby::middleware::Group bar("mygroup.bar");
constexpr goby::middleware::Group ctd("another.ctd");
