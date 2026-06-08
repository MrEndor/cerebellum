import type { BackendsResponse, BackendStatus } from '../types'

const BADGE: Record<BackendStatus, string> = {
  up: 'bg-green-100 text-green-800',
  down: 'bg-red-100 text-red-800',
  draining: 'bg-yellow-100 text-yellow-800',
  unknown: 'bg-slate-100 text-slate-500',
}

function lastCheck(ms: number): string {
  return `${(ms / 1000).toFixed(1)}s ago`
}

function LoadBar({ flows, total }: { flows: number; total: number }) {
  const pct = total > 0 ? (flows / total) * 100 : 0
  return (
    <div className="flex items-center justify-end gap-2">
      <div className="h-2 w-24 overflow-hidden rounded bg-slate-100">
        <div
          className="h-full rounded bg-sky-500"
          style={{ width: `${pct.toFixed(1)}%` }}
        />
      </div>
      <span className="w-28 text-right tabular-nums text-slate-600">
        {flows.toLocaleString()} ({pct.toFixed(0)}%)
      </span>
    </div>
  )
}

export function BackendsTable({ data }: { data: BackendsResponse | null }) {
  if (!data) {
    return (
      <div className="px-6 py-4 text-sm text-slate-400">
        Waiting for controlplane…
      </div>
    )
  }

  const totalFlows = data.backends.reduce((sum, b) => sum + b.flows, 0)

  return (
    <div className="px-6 py-4">
      <div className="mb-3 flex items-center gap-3">
        <span className="font-semibold text-slate-900">
          VIP {data.vip}:{data.vip_port}
        </span>
        <span className="rounded bg-slate-100 px-2 py-0.5 text-xs text-slate-500">
          {data.backends.length} backends
        </span>
        <span className="rounded bg-slate-100 px-2 py-0.5 text-xs text-slate-500">
          {totalFlows.toLocaleString()} flows
        </span>
      </div>
      <table className="w-full overflow-hidden rounded-lg border border-slate-200 bg-white text-sm">
        <thead>
          <tr className="border-b border-slate-200 bg-slate-50 text-left text-slate-500">
            <th className="px-4 py-2 font-medium">Backend</th>
            <th className="px-4 py-2 font-medium">Health</th>
            <th className="px-4 py-2 text-right font-medium">Load (flows)</th>
            <th className="px-4 py-2 text-right font-medium">Failures</th>
            <th className="px-4 py-2 text-right font-medium">Last check</th>
          </tr>
        </thead>
        <tbody>
          {data.backends.map((b) => (
            <tr
              key={`${b.ip}:${b.port}`}
              className="border-t border-slate-100 transition-colors hover:bg-slate-50"
            >
              <td className="px-4 py-2.5 font-medium text-slate-800">
                {b.ip}:{b.port}
              </td>
              <td className="px-4 py-2.5">
                <span
                  className={`rounded px-2 py-0.5 text-xs font-medium ${BADGE[b.status]}`}
                >
                  {b.status.toUpperCase()}
                </span>
              </td>
              <td className="px-4 py-2.5">
                <LoadBar flows={b.flows} total={totalFlows} />
              </td>
              <td className="px-4 py-2.5 text-right text-slate-600">
                {b.fail_count}
              </td>
              <td className="px-4 py-2.5 text-right text-slate-400">
                {lastCheck(b.last_check_ms)}
              </td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  )
}
