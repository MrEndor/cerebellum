export type DataplaneState = 'connected' | 'detached'

export interface Stats {
  dataplane: DataplaneState
  rx_pps: number
  tx_pps: number
  dropped_pps: number
  new_flows_ps: number
  active_flows: number
}

export type BackendStatus = 'up' | 'down' | 'draining' | 'unknown'

export interface Backend {
  ip: string
  port: number
  status: BackendStatus
  fail_count: number
  last_check_ms: number
  flows: number
}

export interface BackendsResponse {
  vip: string
  vip_port: number
  backends: Backend[]
}
