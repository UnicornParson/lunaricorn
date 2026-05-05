#pragma once

#include "endpoint.h"
namespace lunaricorn
{
class HTTP_Endpoint: public Endpoint
{
    HTTP_Endpoint();
    ~HTTP_Endpoint();
    virtual void handleEvent(const EventData& event) override;
    virtual bool start()  override;
    virtual bool stop()  override;
};
} // namespace lunaricorn
