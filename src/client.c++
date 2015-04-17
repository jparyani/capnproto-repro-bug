// Copyright (c) 2013-2014 Sandstorm Development Group, Inc. and contributors
// Licensed under the MIT License:
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include <kj/debug.h>
#include <kj/async-io.h>
#include <capnp/rpc-twoparty.h>
#include <math.h>
#include <iostream>

#include <sandstorm/supervisor.capnp.h>

using namespace capnp;
using namespace sandstorm;

class SandstormCoreImpl final: public SandstormCore::Server {
  kj::Promise<void> getOwnerNotificationTarget(GetOwnerNotificationTargetContext context) override {
    std::cout << "success2" << std::endl;
    return kj::READY_NOW;
  }
};

int main(int argc, const char* argv[]) {
  if (argc != 2) {
    std::cerr << "usage: " << argv[0] << " HOST:PORT\n"
        "Connects to the server at the given address and "
        "does some RPCs." << std::endl;
    return 1;
  }

  auto io = kj::setupAsyncIo();
  Capability::Client cap = kj::heap<SandstormCoreImpl>();
  auto promise = io.provider->getNetwork().parseAddress(argv[1]).then([&](auto address) {
    return address->connect().then([&](auto connection) {
      return kj::mv(connection);
    });
  });

  auto connection = promise.wait(io.waitScope);
  TwoPartyVatNetwork network(*connection, rpc::twoparty::Side::CLIENT);
  RpcSystem<rpc::twoparty::SturdyRefHostId> rpcSystem(network, cap);

  MallocMessageBuilder message(4);
  auto vatId = message.getRoot<rpc::twoparty::VatId>();
  vatId.setSide(rpc::twoparty::Side::SERVER);
  auto api = rpcSystem.bootstrap(vatId).castAs<SandstormApi<> >();
  api.stayAwakeRequest().send().wait(io.waitScope);

  return 0;
}
