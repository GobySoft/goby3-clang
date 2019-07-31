#include "interface.h"



int main()
{
    goby::middleware::InterProcessTransporter interprocess;
    interprocess.subscribe<bar, const char*>();
}
