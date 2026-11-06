# RX256K 验证记录

WNS +0.035 ns / WHS +0.010 ns / WPWS +0.437 ns；完整功能与独立物理审计通过，新版实板成绩待用户烧录后确认。

```json
{
  "build_tag": "TX64-RX256K-N57B",
  "physical_metrics_ns": {
    "setup_slack_ns": 0.035,
    "hold_slack_ns": 0.01,
    "pulse_width_slack_ns": 0.437
  },
  "actual_receive_capacity_bytes": 262144,
  "actual_window_scale": 3,
  "tx_banks": 64,
  "tx_congestion_cap_bytes": 131072,
  "tests": {
    "application": 7,
    "negative": 11,
    "full_gmii": 3,
    "windows_app": 2,
    "transport": 13,
    "receive_zero_window_rounds": 3,
    "partial_reopen_bytes": 16,
    "rx_bridge_frames": 1024
  },
  "board_programmed_by_agent": false,
  "new_board_throughput_measured": false,
  "bit_sha256": "4871542713694d7d25553c67d46c0a6a56c961e8f53513583e9942118301f8b0"
}
```

详细来源见physical-verification.json、functional-verification.json、rx-window-ab-verification.json及对应原始报告。CDC报告中的已知复位结构按原审计规则逐项核验，不将其描述为零CDC告警。


## 实板测试补充（2026-10-25）

已通过JTAG加载并测量，当前板上TX64-RX256K-N57B。默认正向约751Mbps，反向w1M约938Mbps；-N组合仍有回退，未解决Windows正向满速。详细报告：`/mnt/e/VM/Share/Valence-rtl/valence-iperf-20261025T071436Z/BOARD-RESULTS.md`；不要把前文封存前的“尚未烧录”当作最新实板状态。全部网卡高级属性已恢复、COM4已释放。补充极短段压力测试出现1次忙丢弃，TCP重传16B后恢复完整载荷；原始失败与恢复记录均保留。
