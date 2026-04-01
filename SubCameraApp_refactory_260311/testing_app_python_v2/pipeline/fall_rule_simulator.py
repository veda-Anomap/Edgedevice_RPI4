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
    fall_velocity_threshold:  float = 18.0
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
    spine_angle:       float        # degrees
    velocity_delta:    float        # 현 프레임 Δcy

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
        self.cy_history:   deque[float] = deque(maxlen=10)
        self.fall_votes:   deque[bool]  = deque(maxlen=win_size)
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
                   track_id: int = 0) -> RuleResult:
        """
        box: (x, y, w, h) in pixels
        kpts: 17 keypoints [(x,y), ...]
        """
        p = self._params
        if len(kpts) < 17:
            return self._empty_result()

        bx, by, bw, bh = box

        # --- 1. 주요 관절 위치 ---
        head_y      = kpts[self.IDX_NOSE][1]
        sh_y        = (kpts[self.IDX_LSH][1]  + kpts[self.IDX_RSH][1])  / 2
        sh_x        = (kpts[self.IDX_LSH][0]  + kpts[self.IDX_RSH][0])  / 2
        hip_y       = (kpts[self.IDX_LHIP][1] + kpts[self.IDX_RHIP][1]) / 2
        hip_x       = (kpts[self.IDX_LHIP][0] + kpts[self.IDX_RHIP][0]) / 2
        knee_y      = (kpts[self.IDX_LKNEE][1]+ kpts[self.IDX_RKNEE][1])/ 2
        ankle_y     = (kpts[self.IDX_LANK][1] + kpts[self.IDX_RANK][1]) / 2

        # --- 2. 지표 계산 ---
        body_height = abs(ankle_y - head_y)
        body_width  = float(bw)
        compression = body_height / (bh + 1e-6)
        dx = abs(sh_x - hip_x)
        dy = abs(sh_y - hip_y) + 1e-6
        angle = math.degrees(math.atan2(dy, dx))   # 0°=수평, 90°=수직

        # --- 3. 하강 속도 ---
        hist = self._history.setdefault(track_id,
                    PersonHistory(p.vote_window_size))
        cy = by + bh / 2.0
        vel_delta = 0.0
        velocity_cond = False
        if hist.cy_history:
            vel_delta = cy - hist.cy_history[-1]
            velocity_cond = vel_delta > p.fall_velocity_threshold
        hist.cy_history.append(cy)

        # --- 4. 조건 판정 ---
        side_a   = body_width > body_height * p.side_fall_aspect_ratio
        side_b   = angle < p.side_fall_angle
        front_a  = compression < p.frontal_fall_compression
        front_b  = angle < p.frontal_fall_angle
        dyn_a    = velocity_cond
        dyn_b    = angle < p.dynamic_fall_angle

        side_fall    = side_a  and side_b
        frontal_fall = front_a and front_b
        dynamic_fall = dyn_a   and dyn_b

        # --- 5. 앉기 필터 ---
        is_sitting = (knee_y > hip_y) and (ankle_y > knee_y) and (angle > p.sitting_angle)

        raw_fall = (side_fall or frontal_fall or dynamic_fall) and not is_sitting

        # --- 6. Voting ---
        hist.fall_votes.append(raw_fall)
        vote_count = sum(hist.fall_votes)
        is_falling = vote_count >= p.vote_threshold

        return RuleResult(
            body_height=body_height,   body_width=body_width,
            compression_ratio=compression, spine_angle=angle,
            velocity_delta=vel_delta,
            side_cond_a=side_a, side_cond_b=side_b,
            frontal_cond_a=front_a, frontal_cond_b=front_b,
            dynamic_cond_a=dyn_a,  dynamic_cond_b=dyn_b,
            is_sitting=is_sitting,
            side_fall=side_fall, frontal_fall=frontal_fall,
            dynamic_fall=dynamic_fall, raw_fall=raw_fall,
            vote_count=vote_count,
            vote_window=len(hist.fall_votes),
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
        velocity_cond = velocity_delta > p.fall_velocity_threshold

        side_a   = body_width > body_height * p.side_fall_aspect_ratio
        side_b   = spine_angle < p.side_fall_angle
        front_a  = compression < p.frontal_fall_compression
        front_b  = spine_angle < p.frontal_fall_angle
        dyn_a    = velocity_cond
        dyn_b    = spine_angle < p.dynamic_fall_angle
        is_sitting = (knee_y > hip_y) and (ankle_y > knee_y) and (spine_angle > p.sitting_angle)

        side_fall    = side_a and side_b
        frontal_fall = front_a and front_b
        dynamic_fall = dyn_a and dyn_b
        raw_fall = (side_fall or frontal_fall or dynamic_fall) and not is_sitting

        # 슬라이더 모드에서는 vote 없이 raw_fall 만 반환
        return RuleResult(
            body_height=body_height, body_width=body_width,
            compression_ratio=compression, spine_angle=spine_angle,
            velocity_delta=velocity_delta,
            side_cond_a=side_a, side_cond_b=side_b,
            frontal_cond_a=front_a, frontal_cond_b=front_b,
            dynamic_cond_a=dyn_a, dynamic_cond_b=dyn_b,
            is_sitting=is_sitting,
            side_fall=side_fall, frontal_fall=frontal_fall,
            dynamic_fall=dynamic_fall, raw_fall=raw_fall,
            vote_count=1 if raw_fall else 0, vote_window=1,
            is_falling=raw_fall,
        )

    @staticmethod
    def _empty_result() -> RuleResult:
        return RuleResult(*([0.0]*5 + [False]*12 + [0, 0, False]))
