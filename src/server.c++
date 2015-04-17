// Hack around stdlib bug with C++14.
#include <initializer_list>  // force libstdc++ to include its config
#undef _GLIBCXX_HAVE_GETS    // correct broken config
// End hack.

#include <kj/async-io.h>
#include <capnp/rpc-twoparty.h>
#include <iostream>

#include <sandstorm/supervisor.capnp.h>

using namespace capnp;
using namespace sandstorm;

class SandstormApiImpl final: public SandstormApi<>::Server {
public:
  SandstormApiImpl(Capability::Client core) : core(core.castAs<SandstormCore>()) {}
  kj::Promise<void> stayAwake(StayAwakeContext context) override {
    std::cout << "success" << std::endl;
    return core.getOwnerNotificationTargetRequest().send().then([context](auto args) mutable {
      context.getResults();
    });
  }

  SandstormCore::Client core;
};

class CapRedirector
    : public capnp::Capability::Server, public kj::Refcounted {
  // A capability which forwards all calls to some target. If the target becomes disconnected,
  // the capability queues new calls until a new target is provided.
  //
  // We use this to handle the fact that the front-end is allowed to restart without restarting
  // all grains. The SandstormCore capability -- provided by the front-end -- will temporarily
  // become disconnected in these cases. We know the front-end will come back up and reestablish
  // the connection soon, but there's nothing we can do except wait, and in the meantime we don't
  // want to spurriously fail calls.

public:
  CapRedirector(kj::PromiseFulfillerPair<capnp::Capability::Client> paf =
                kj::newPromiseAndFulfiller<capnp::Capability::Client>())
      : target(kj::mv(paf.promise)),
        fulfiller(kj::mv(paf.fulfiller)) {}

  uint setTarget(capnp::Capability::Client newTarget) {
    ++iteration;
    target = newTarget;

    // If the previous target was a promise target, fulfill it.
    fulfiller->fulfill(kj::mv(newTarget));

    return iteration;
  }

  void setDisconnected(uint oldIteration) {
    if (iteration == oldIteration) {
      // Our current client was disconnected.
      ++iteration;
      auto paf = kj::newPromiseAndFulfiller<capnp::Capability::Client>();
      target = kj::mv(paf.promise);
      fulfiller = kj::mv(paf.fulfiller);
    }
  }

  kj::Promise<void> dispatchCall(
      uint64_t interfaceId, uint16_t methodId,
      capnp::CallContext<capnp::AnyPointer, capnp::AnyPointer> context) override {
    capnp::AnyPointer::Reader params = context.getParams();
    auto req = target.typelessRequest(interfaceId, methodId, params.targetSize());
    req.set(params);
    return context.tailCall(kj::mv(req));
  }

private:
  uint iteration = 0;
  capnp::Capability::Client target;
  kj::Own<kj::PromiseFulfiller<capnp::Capability::Client>> fulfiller;
};

int main(int argc, const char* argv[]) {
  if (argc != 2) {
    std::cerr << "usage: " << argv[0] << " ADDRESS[:PORT]\n"
        "Runs the server bound to the given address/port.\n"
        "ADDRESS may be '*' to bind to all local addresses.\n"
        ":PORT may be omitted to choose a port automatically." << std::endl;
    return 1;
  }

  auto io = kj::setupAsyncIo();
  kj::Own<CapRedirector> redirector(kj::refcounted<CapRedirector>());
  kj::Own<kj::ConnectionReceiver> receiver;
  auto promise = io.provider->getNetwork().parseAddress(argv[1]).then([&](auto address) {
    receiver = address->listen();

    return receiver->accept().then([&](auto _connection) {
      return kj::mv(_connection);
    });
  });

  auto connection = promise.wait(io.waitScope);
  TwoPartyVatNetwork network(*connection, rpc::twoparty::Side::SERVER);
  Capability::Client coreCap = kj::addRef(*redirector);
  Capability::Client cap = kj::heap<SandstormApiImpl>(coreCap);
  RpcSystem<rpc::twoparty::SturdyRefHostId> rpcSystem(network, cap);

  MallocMessageBuilder message(4);
  auto vatId = message.getRoot<rpc::twoparty::VatId>();
  vatId.setSide(rpc::twoparty::Side::CLIENT);
  auto core = rpcSystem.bootstrap(vatId).castAs<SandstormCore>();
  redirector->setTarget(core);

  kj::NEVER_DONE.wait(io.waitScope);
}
