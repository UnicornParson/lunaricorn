#pragma once
#include "stdafx.h"
#include <string>
#include <vector>
#include <memory>
#include <Poco/SharedPtr.h>
#include "signaling_engine.h"

namespace lunaricorn {

class SignalingEngineTest
{
public:
    explicit SignalingEngineTest(std::shared_ptr<SignalingEngine> engine);
    ~SignalingEngineTest() = default;

    bool run();

private:
    std::shared_ptr<SignalingEngine> engine_;
    
    bool testCreateEvent();
    bool testGetUniqueValues();
    bool testFindEvents();
    bool testFindEventsByType();
    bool testDatabaseInitialization();
};

} // namespace lunaricorn