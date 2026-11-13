// Build without any HTTP library: the online features report that the network is not
// available in this build; the rest of the game is unaffected.

#include "game/online/HttpTransport.h"

#if !defined(COSMIC_HTTP_CURL) && !defined(COSMIC_HTTP_WINHTTP)

namespace cosmic {

namespace {
class NoTransport final : public IHttpTransport {
public:
    const char* name() const override { return "aucun"; }
    HttpResponse perform(const HttpRequest&) override {
        HttpResponse r;
        r.error = NetError::Unsupported;
        r.errorMessage = "cette version du jeu a été compilée sans client HTTP";
        return r;
    }
};
}  // namespace

std::unique_ptr<IHttpTransport> makePlatformTransport() { return std::make_unique<NoTransport>(); }

}  // namespace cosmic

#endif
