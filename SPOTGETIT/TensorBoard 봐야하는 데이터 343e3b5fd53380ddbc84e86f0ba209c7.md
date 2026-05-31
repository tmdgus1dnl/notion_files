# TensorBoard 봐야하는 데이터

## 매번 반드시 보는 것 (1순위)

**Train/mean_reward**와 **Train/mean_episode_length** 두 개가 전체 학습 상태를 가장 빠르게 요약해 줍니다. mean_reward가 올라가면서 mean_episode_length도 올라가면 학습이 잘 되고 있는 겁니다. 둘 중 하나만 올라가면 문제가 있어요. 예를 들어 mean_reward는 올라가는데 episode_length가 짧아지면, 짧은 시간 안에 보상을 많이 먹고 바로 넘어지는 꼼수를 학습한 것입니다.

**Episode/rew_xxx 항목들**은 reward를 추가할 때마다 확인합니다. 새 reward를 켰을 때 기존에 잘 올라가던 항목(예: rew_tracking_lin_vel)이 무너지는지 확인하는 게 핵심이에요. 항목이 많아질수록 여기가 중요해집니다. 지금은 tracking_lin_vel과 termination 두 개뿐이라 간단하지만, Step 5~6까지 가면 8개 이상의 reward가 동시에 돌아가니까 어떤 항목이 서로 충돌하는지 여기서 파악합니다.

---

## 문제 생겼을 때 보는 것 (2순위)

**Policy/mean_noise_std**는 학습이 이상할 때 원인 파악용입니다. 정상적인 흐름은 초반에 올라갔다가(탐색) 중후반에 내려가는(수렴) 것인데요. 1000 iteration이 넘어도 계속 올라가기만 하면 reward 신호가 너무 약하거나 모순이 있어서 정책이 수렴을 못 하고 있다는 뜻입니다. 이 경우 reward 가중치를 재검토해야 합니다.

**Loss/surrogate**는 학습 안정성 진단용입니다. 보통은 볼 필요 없는데, mean_reward가 갑자기 급락하거나 학습이 발산하는 느낌이 들 때 확인하세요. 이 값이 갑자기 크게 튀면 학습률이 너무 높거나 reward 스케일이 너무 커서 gradient가 폭발한 겁니다.

**Loss/value_function**도 비슷한 용도인데, 이 값이 내려가다가 다시 올라가면 reward 구조가 바뀌어서 value network가 헷갈리고 있다는 신호예요. reward를 한꺼번에 여러 개 추가했을 때 이런 현상이 나타날 수 있고, 그래서 하나씩 추가하라고 하는 겁니다.

# play 데이터

## 1행: 속도 추종 (가장 중요)

**Base velocity x**: commanded(주황)가 0 근처인데 measured(파랑)가 -0.2까지 갔다가 돌아오고 있어요. 명령은 거의 정지인데 앞뒤로 흔들리고 있다는 뜻입니다. 아직 정밀한 추종은 안 되지만 Step 1에서는 정상이에요.

**Base velocity y**: commanded 0인데 measured가 0.1까지 튀고 있어요. 횡방향으로 밀리고 있다는 건데, 나중에 orientation reward를 켜면 개선됩니다.

**Base velocity yaw**: 회전도 비슷하게 명령 대비 흔들리고 있는데, Step 2에서 tracking_ang_vel을 추가하면 잡힐 부분입니다.

## 2행: 관절 상태

**DOF Position**: measured(파랑)와 target(주황)의 차이가 큽니다. target은 거의 일정한데 measured가 크게 진동하고 있어요. 이건 PD 게인(Kp=50)이 관절을 목표로 정확히 잡아주지 못하고 있다는 뜻일 수 있는데, 지금은 RL이 아직 깨끗한 액션을 못 내고 있어서 그런 거라 Step 4~5에서 action_rate reward를 켜면 줄어듭니다.

**Joint Velocity**: ±5~7 rad/s로 꽤 크게 왔다갔다 하고 있어요. 동작이 거칠다는 뜻인데 역시 action_rate, torques reward 추가 후 개선 대상입니다.

## 3행: 접촉과 토크

**Vertical Contact Forces**: 0.2초 근처에서 한 번 접촉이 있고 그 이후 거의 0이에요. 이건 발이 지면에 제대로 안 닿고 있거나, 넘어져서 접촉이 끊긴 상황일 수 있습니다. 나중에 trot gait이 나오면 여기에 규칙적인 접촉 패턴(발이 번갈아 땅을 딛는)이 보여야 해요.

**Torque**: ±3 N·m까지 나오고 있는데, URDF에서 effort limit이 2.94 N·m로 설정되어 있으니 한계 근처까지 쓰고 있는 겁니다. dof_pos_limits reward를 나중에 켜면 이 부분도 억제됩니다.