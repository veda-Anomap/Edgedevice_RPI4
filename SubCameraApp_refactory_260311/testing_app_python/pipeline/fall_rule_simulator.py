"""
fall_rule_simulator.py – FallDetector.cpp + AppConfig.h 의 Python 포트
키포인트 배열 또는 슬라이더 값으로 낙상 판정 룰을 시뮬레이션한다.
"""
import math
from collections import deque
from dataclasses import dataclass, field
from typing import NamedTuple


# ==============================================================
# 파라미터 구조체 (AppConfig 대응)
# ==============================================================
@dataclass
class FallRuleParams:
    confidence_threshold:     float = 0.5
    fall_velocity_threshold:  float = 18.0   # Legacy
    fall_norm_velocity_thresh:float = 1.2    # Advanced (Torso units/sec)
    stillness_thresh_sec:     float = 1.5
    ground_zone_ratio:        float = 0.8
    side_fall_aspect_ratio:   float = 0.9
    side_fall_angle:          float = 45.0
    frontal_fall_compression: float = 0.45
    frontal_fall_angle:       float = 50.0
    dynamic_fall_angle:       float = 55.0
    sitting_angle:            float = 50.0
    vote_window_size:         int   = 6
    vote_threshold:           int   = 2


# ==============================================================
# 규칙 적용 결과 (디버깅 / 시각화용)
# ==============================================================
class RuleResult(NamedTuple):
    # 계산된 지표
    body_height:       float
    body_width:        float
    compression_ratio: float
    spine_angle:       float
    velocity_delta:    float        # Pixel Δcy
    norm_velocity:     float        # Torso normalized velocity
    current_state:     str          # STANDING, FALLING, LYING_DOWN

    # 개별 조건 충족 여부
    side_cond_a:   bool  # body_width > body_height * ratio
    side_cond_b:   bool  # angle < side_fall_angle
    frontal_cond_a:bool  # compression < threshold
    frontal_cond_b:bool  # angle < frontal_angle
    dynamic_cond_a:bool  # velocity > threshold
    dynamic_cond_b:bool  # angle < dynamic_angle
    is_sitting:    bool  # 앉기 필터

    # 복합 판정
    side_fall:    bool
    frontal_fall: bool
    dynamic_fall: bool
    raw_fall:     bool

    # 투표 결과
    vote_count:  int
    vote_window: int
    is_falling:  bool   # 최종 확정


# ==============================================================
# 개인별 이력 (트래킹)
# ==============================================================
class PersonHistory:
    def __init__(self, win_size: int = 6):
        self.state: str = "STANDING" # STANDING, FALLING, LYING_DOWN
        self.cy_history:   deque[float] = deque(maxlen=10)
        self.fall_votes:   deque[bool]  = deque(maxlen=win_size)
        self.last_update_time: float = 0.0
        self.still_start_time: float = 0.0
        self.filtered_torso_length: float = -1.0
        self.already_saved: bool = False

    def resize_vote_window(self, win: int):
        votes = list(self.fall_votes)
        self.fall_votes = deque(votes[-win:], maxlen=win)


# ==============================================================
# FallRuleSimulator
# ==============================================================
class FallRuleSimulator:
    """
    FallDetector.cpp 의 낙상 판정 로직을 Python으로 재현.
    params 변경 시 update_params()를 호출하여 즉시 반영.
    """

    # COCO keypoint indices
    IDX_NOSE   = 0
    IDX_LSH, IDX_RSH = 5, 6
    IDX_LHIP, IDX_RHIP = 11, 12
    IDX_LKNEE, IDX_RKNEE = 13, 14
    IDX_LANK, IDX_RANK   = 15, 16

    def __init__(self, params: FallRuleParams | None = None):
        self._params = params or FallRuleParams()
        self._history: dict[int, PersonHistory] = {}

    def update_params(self, p: FallRuleParams):
        # vote_window 변경 시 이력 deque 크기 조정
        if p.vote_window_size != self._params.vote_window_size:
            for h in self._history.values():
                h.resize_vote_window(p.vote_window_size)
        self._params = p

    # ------------------------------------------------------------------
    def check_fall(self,
                   box: tuple[int,int,int,int],   # (x,y,w,h)
                   kpts: list[tuple[int,int]],    # 17 keypoints (x,y)
                   track_id: int = 0,
                   img_h: int = 480) -> RuleResult:
        """
        box: (x, y, w, h) in pixels
        kpts: 17 keypoints [(x,y), ...]
        """
        p = self._params
        if len(kpts) < 17:
            return self._empty_result()

        bx, by, bw, bh = box
        
        # --- 0. 히스토리 객체 확보 (중요: 이 부분이 누락되어 에러 발생) ---
        if track_id not in self._history:
            self._history[track_id] = PersonHistory(p.vote_window_size)
        hist = self._history[track_id]

        # --- 1. 시간 및 정규화용 지표 계산 ---
        import time
        t_now = time.time()
        dt = t_now - hist.last_update_time if hist.last_update_time > 0 else 0.033
        hist.last_update_time = t_now

        head_y = kpts[self.IDX_NOSE][1]
        sh_y   = (kpts[self.IDX_LSH][1] + kpts[self.IDX_RSH][1]) / 2
        sh_x   = (kpts[self.IDX_LSH][0] + kpts[self.IDX_RSH][0]) / 2
        hip_y  = (kpts[self.IDX_LHIP][1] + kpts[self.IDX_RHIP][1]) / 2
        hip_x  = (kpts[self.IDX_LHIP][0] + kpts[self.IDX_RHIP][0]) / 2
        ankle_y= (kpts[self.IDX_LANK][1] + kpts[self.IDX_RANK][1]) / 2

        torso_len = math.sqrt((sh_x - hip_x)**2 + (sh_y - hip_y)**2) + 1e-6
        if hist.filtered_torso_length < 0:
            hist.filtered_torso_length = torso_len
        else:
            hist.filtered_torso_length = 0.8 * hist.filtered_torso_length + 0.2 * torso_len

        # --- 2. 기본 지표 ---
        body_height = abs(ankle_y - head_y)
        body_width  = float(bw)
        compression = body_height / (bh + 1e-6)
        dx = abs(sh_x - hip_x)
        dy = abs(sh_y - hip_y) + 1e-6
        angle = math.degrees(math.atan2(dy, dx))

        # --- 3. 속도 및 물리 지표 ---
        cy = by + bh / 2.0
        pixel_vel = 0.0
        if hist.cy_history and dt > 0.001:
            pixel_vel = (cy - hist.cy_history[-1]) / dt
        norm_vel = pixel_vel / hist.filtered_torso_length
        hist.cy_history.append(cy)

        # --- 4. 상태 머신 탈피 (1 FPS 대응 Stateless 로직) ---
        # 원인: AI 딜레이(1 FPS)로 트래커 연속성 및 속도 측정이 불가능하므로,
        # 현재 프레임의 자세(Posture)만 보고 즉각적으로 낙상을 판정하도록 단순화함.
        
        is_horizontal = angle < p.dynamic_fall_angle
        is_on_ground = head_y > (p.ground_zone_ratio * img_h)
        
        raw_fall = is_horizontal and is_on_ground
        
        # 레거시 측면 낙상 보완
        side_fall = (body_width > body_height * p.side_fall_aspect_ratio) and (angle < p.side_fall_angle)
        frontal_fall = (compression < p.frontal_fall_compression) and (angle < p.frontal_fall_angle)
        
        # 이전에는 1.5초 누워있어야(LYING_DOWN) 했으나, 1 FPS 환경에서는 즉시 낙상 판정
        is_falling = raw_fall or side_fall or frontal_fall
        
        # 시각화(UI)를 위해 가상 상태 매핑
        if is_falling:
            hist.state = "LYING_DOWN"
        else:
            hist.state = "STANDING"

        return RuleResult(
            body_height=body_height, body_width=body_width,
            compression_ratio=compression, spine_angle=angle,
            velocity_delta=pixel_vel * dt, norm_velocity=norm_vel,
            current_state=hist.state,
            side_cond_a=(body_width > body_height * p.side_fall_aspect_ratio),
            side_cond_b=(angle < p.side_fall_angle),
            frontal_cond_a=(compression < p.frontal_fall_compression),
            frontal_cond_b=(angle < p.frontal_fall_angle),
            dynamic_cond_a=(norm_vel > p.fall_norm_velocity_thresh),
            dynamic_cond_b=(angle < p.dynamic_fall_angle),
            is_sitting=(head_y < (p.ground_zone_ratio - 0.2) * img_h), # Simple proxy
            side_fall=side_fall, frontal_fall=frontal_fall,
            dynamic_fall=(hist.state == "FALLING"), raw_fall=raw_fall,
            vote_count=1 if is_falling else 0,
            vote_window=1,
            is_falling=is_falling,
        )

    def reset(self):
        self._history.clear()

    # ------------------------------------------------------------------
    # 슬라이더 모드용: 지표로 직접 시뮬레이션 (kpts 없이)
    # ------------------------------------------------------------------
    def simulate_from_metrics(self,
                              body_height: float,
                              body_width: float,
                              box_height: float,
                              spine_angle: float,
                              velocity_delta: float,
                              knee_y: float,
                              hip_y: float,
                              ankle_y: float) -> RuleResult:
        """실제 kpts 없이 계산된 지표값으로 직접 룰 판정 (슬라이더 모드)"""
        p = self._params
        compression = body_height / (box_height + 1e-6)
        
        # 슬라이더 모드에서는 torso_length를 body_height로부터 추정 (임시)
        torso_est = body_height * 0.4
        norm_vel = (velocity_delta / 0.033) / torso_est if torso_est > 0 else 0
        
        side_a   = body_width > body_height * p.side_fall_aspect_ratio
        side_b   = spine_angle < p.side_fall_angle
        front_a  = compression < p.frontal_fall_compression
        front_b  = spine_angle < p.frontal_fall_angle
        is_sitting = (knee_y > hip_y) and (ankle_y > knee_y) and (spine_angle > p.sitting_angle)

        side_fall    = side_a and side_b
        raw_fall = (norm_vel > p.fall_norm_velocity_thresh and spine_angle < p.dynamic_fall_angle)
        is_falling = raw_fall or side_fall

        return RuleResult(
            body_height=body_height, body_width=body_width,
            compression_ratio=compression, spine_angle=spine_angle,
            velocity_delta=velocity_delta, norm_velocity=norm_vel,
            current_state="MANUAL",
            side_cond_a=side_a, side_cond_b=side_b,
            frontal_cond_a=front_a, frontal_cond_b=front_b,
            dynamic_cond_a=(norm_vel > p.fall_norm_velocity_thresh),
            dynamic_cond_b=(spine_angle < p.dynamic_fall_angle),
            is_sitting=is_sitting,
            side_fall=side_fall, frontal_fall=front_a and front_b,
            dynamic_fall=raw_fall, raw_fall=raw_fall,
            vote_count=1 if is_falling else 0, vote_window=1,
            is_falling=is_falling,
        )

    @staticmethod
    def _empty_result() -> RuleResult:
        return RuleResult(*([0.0]*6 + ["NONE"] + [False]*12 + [0, 0, False]))
