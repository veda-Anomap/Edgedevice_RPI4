#ifndef PACKET_PROTOCOL_H
#define PACKET_PROTOCOL_H

#include <cstdint>

// --- 사용자 정의 패킷 규격 ---
// 프로토콜 계층: 네트워크 구현과 독립적으로 정의

enum class MessageType : uint8_t {
  ACK = 0x00,       // 확인 응답
  LOGIN = 0x01,     // 로그인 요청
  SUCCESS = 0x02,   // 성공 응답 (JSON 없음)
  FAIL = 0x03,      // 실패 응답
  DEVICE = 0x04,    // 장치 제어 (추후 사용)
  AVAILABLE = 0x05, // 장치 사용 수치
  AI = 0x06,        // AI 관련 메타데이터
  CAMERA = 0x07,    // 카메라 리스트
  ASSIGN = 0x08,    // 회원 등록
  META = 0x09,      // 센서 데이터
  IMAGE = 0x0a      // 이미지 전송
};

// 패킷 헤더 (총 5바이트: Type 1 + BodyLength 4)
#pragma pack(push, 1)
struct PacketHeader {
  MessageType type;     // 메시지 종류 (1바이트)
  uint32_t body_length; // JSON 본문 크기 (4바이트, 네트워크 바이트 오더)
};
#pragma pack(pop)

#endif // PACKET_PROTOCOL_H
