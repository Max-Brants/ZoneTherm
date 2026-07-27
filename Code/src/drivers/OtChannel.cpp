#include "OtChannel.h"

#include "../vendor/OpenTherm/OpenTherm.h"

OtChannel::OtChannel() = default;
OtChannel::~OtChannel() = default;

void OtChannel::begin(int inPin, int outPin, RequestHandler handler) {
    handler_ = std::move(handler);
    ot_ = std::make_unique<OpenTherm>(inPin, outPin, /*isSlave=*/true);
    ot_->begin([this](unsigned long request, OpenThermResponseStatus status) {
        handler_(static_cast<uint32_t>(request),
                 status == OpenThermResponseStatus::SUCCESS);
    });
}

void OtChannel::poll() {
    if (ot_) ot_->process();
}

void OtChannel::sendResponse(uint32_t frame) {
    if (ot_) ot_->sendResponse(frame);
}
