// One OpenTherm slave channel: thin I/O wrapper around the vendored
// OpenTherm driver so services only deal in raw frames + a validity flag,
// never in vendor types. No protocol logic here - that's domain/OtResponder.

#pragma once

#include <cstdint>
#include <functional>
#include <memory>

class OpenTherm;

class OtChannel {
public:
    // valid=false means the driver flagged the frame (parity, timeout or
    // framing errors) - the request payload is then meaningless.
    using RequestHandler = std::function<void(uint32_t request, bool valid)>;

    OtChannel();
    ~OtChannel();

    // Blocks ~1 s (the vendored driver's line-settle delay) - call from the
    // OT task, not from app_main.
    void begin(int inPin, int outPin, RequestHandler handler);

    // Cheap poll; fires the handler when the ISR has assembled a frame.
    void poll();

    // Bit-bangs the response (~34 ms busy-wait). Serialize calls across
    // channels - concurrent sends corrupt each other's bit timing.
    void sendResponse(uint32_t frame);

private:
    std::unique_ptr<OpenTherm> ot_;
    RequestHandler handler_;
};
