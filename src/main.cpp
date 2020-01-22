#include "sgct/sgct.h"
#include "websockethandler.h"
#include <memory>
#include <string>
#include <vector>


namespace {
    std::unique_ptr<WebSocketHandler> wsHandler;


    int64_t exampleInt = 0;
    std::string exampleString;
} // namespace


void initOGL(GLFWwindow*) {
    // Perform OpenGL initialization here, loading textures, shaders, etc that are static
    // throughout the application lifetime


}


void preSync() {
    // Do the application simulation step on the server node in here and make sure that
    // the computed state is serialized and deserialized in the encode/decode calls


    if (sgct::Engine::instance().isMaster()) {
        if (sgct::Engine::instance().currentFrameNumber() % 10 == 0) {

            sgct::Log::Info("Queing %i", sgct::Engine::instance().currentFrameNumber());
            wsHandler->queueMessage(
                std::to_string(sgct::Engine::instance().currentFrameNumber())
            );
        }

        wsHandler->tick();
    }
}


std::vector<std::byte> encode() {
    // These are just two examples;  remove them and replace them with the logic of your
    // application that you need to synchronize
    std::vector<std::byte> data;
    sgct::serializeObject(data, exampleInt);
    sgct::serializeObject(data, exampleString);


    return data;
}


void decode(const std::vector<std::byte>& data) {
    // These are just two examples;  remove them and replace them with the logic of your
    // application that you need to synchronize
    unsigned int pos = 0;
    sgct::deserializeObject(data, pos, exampleInt);
    sgct::deserializeObject(data, pos, exampleString);


}


void postSyncPreDraw() {
    // Apply the (now synchronized) application state before the rendering will start
}


void draw(const sgct::RenderData& data) {
    // Do the rendering in here using the provided projection matrix

    const glm::mat4 projectionMatrix = data.modelViewProjectionMatrix;


}


void cleanup() {
    // Cleanup all of your state, particularly the OpenGL state in here.  This function
    // should behave symmetrically to the initOGL function


}


void keyboard(sgct::Key key, sgct::Modifier modifier, sgct::Action action, int) {
    using namespace sgct;

    if (key == Key::Esc && action == Action::Press) {
        Engine::instance().terminate();
    }

    if (key == Key::Space && modifier == Modifier::Shift && action == Action::Release) {
        Log::Info("Released space key");
    }
}


void connectionEstablished() {
    sgct::Log::Info("Connection established");


}


void connectionClosed() {
    sgct::Log::Info("Connection closed");


}


void messageReceived(const void* data, size_t length) {
    std::string_view msg = std::string_view(reinterpret_cast<const char*>(data), length);
    sgct::Log::Info("Message received: %s", msg.data());


}


int main(int argc, char** argv) {
    std::vector<std::string> arg(argv + 1, argv + argc);
    sgct::Configuration config = sgct::parseArguments(arg);
    sgct::config::Cluster cluster = sgct::loadCluster(config.configFilename);

    sgct::Engine::Callbacks callbacks;
    callbacks.initOpenGL = initOGL;
    callbacks.preSync = preSync;
    callbacks.encode = encode;
    callbacks.decode = decode;
    callbacks.postSyncPreDraw = postSyncPreDraw;
    callbacks.draw = draw;
    callbacks.cleanUp = cleanup;
    callbacks.keyboard = keyboard;

    try {
        sgct::Engine::create(cluster, callbacks, config);
    }
    catch (const std::runtime_error & e) {
        sgct::Log::Error("%s", e.what());
        sgct::Engine::destroy();
        return EXIT_FAILURE;
    }

    if (sgct::Engine::instance().isMaster()) {
        wsHandler = std::make_unique<WebSocketHandler>(
            "localhost",
            80,
            connectionEstablished,
            connectionClosed,
            messageReceived
        );
        constexpr const int MessageSize = 1024;
        wsHandler->connect("example-protocol", MessageSize);
    }

    sgct::Engine::instance().setStatsGraphVisibility(true);

    sgct::Engine::instance().render();

    sgct::Engine::destroy();
    return EXIT_SUCCESS;
}
