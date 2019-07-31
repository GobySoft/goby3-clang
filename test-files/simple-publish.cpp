#include "interface.h"

int main()
{
    goby::middleware::InterProcessTransporter interprocess;
    auto b = bar.c_str();
    interprocess.publish<bar>("foo");
    interprocess.publish<ctd>(3);
    interprocess.baz();
}
