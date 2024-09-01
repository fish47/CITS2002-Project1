#include <cppunit/extensions/HelperMacros.h>

namespace runml {

class BaseTextFixture : public CppUnit::TestFixture {
public:
    virtual void setUp() override;
    virtual void tearDown() override;

protected:
    void setMaxAllocSize(size_t size);
    void setMaxAllocCount(size_t count);
};
}
