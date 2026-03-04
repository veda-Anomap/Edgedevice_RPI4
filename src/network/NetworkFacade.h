#ifndef NETWORK_FACADE_H
#define NETWORK_FACADE_H

#include "BeaconService.h"
#include "CommandServer.h"
#include "ICommandReceiver.h"
#include "INetworkSender.h"


#include <memory>

// =============================================
// NetworkFacade: 네트워크 통합 파사드 (SRP + DIP)
// INetworkSender + ICommandReceiver 모두 구현
// BeaconService + CommandServer를 조합하여 관리
// =============================================

class NetworkFacade : public INetworkSender, public ICommandReceiver {
public:
  NetworkFacade();
  ~NetworkFacade() override;

  // ICommandReceiver 구현
  void start(CommandCallback callback) override;
  void stop() override;

  // INetworkSender 구현
  void sendMessage(const std::string &msg) override;
  void sendImage(const std::string &metadata,
                 const std::vector<uint8_t> &jpeg_data) override;

private:
  std::unique_ptr<BeaconService> beacon_;
  std::unique_ptr<CommandServer> command_server_;
};

#endif // NETWORK_FACADE_H
