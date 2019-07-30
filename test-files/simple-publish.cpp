
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
    template <const Group& group, typename Data>
    void publish(const Data& data)
    {
    }

    void baz()
        {
        }
    
};

class InterProcessTransporter : public StaticTransporterInterface
{
};  



constexpr Group bar("mygroup.bar");

int main()
{
    InterProcessTransporter interprocess;
    auto b = bar.c_str();
    interprocess.publish<bar>("foo");
    interprocess.baz();
}
