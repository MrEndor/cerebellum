import type { Stats } from '../types'

function Sparkline({ values, stroke }: { values: number[]; stroke: string }) {
  const width = 120
  const height = 28
  if (values.length < 2) {
    return <div className="mt-2 h-7" />
  }
  const max = Math.max(...values, 1)
  const min = Math.min(...values, 0)
  const span = max - min || 1
  const step = width / (values.length - 1)
  const points = values
    .map((v, i) => {
      const x = (i * step).toFixed(1)
      const y = (height - ((v - min) / span) * height).toFixed(1)
      return `${x},${y}`
    })
    .join(' ')
  return (
    <svg
      viewBox={`0 0 ${width} ${height}`}
      className="mt-2 h-7 w-full"
      preserveAspectRatio="none"
    >
      <polyline
        points={points}
        fill="none"
        stroke={stroke}
        strokeWidth="1.5"
        strokeLinejoin="round"
        vectorEffect="non-scaling-stroke"
      />
    </svg>
  )
}

interface CardProps {
  label: string
  value: number
  color: string
  stroke: string
  series: number[]
  sub?: string
  alert?: boolean
}

function Card({ label, value, color, stroke, series, sub, alert }: CardProps) {
  return (
    <div
      className={`rounded-lg border p-4 text-center ${
        alert ? 'border-red-200 bg-red-50' : 'border-slate-200 bg-white'
      }`}
    >
      <div className={`text-xl font-bold ${color}`}>{value.toLocaleString()}</div>
      <div className="mt-1 text-xs text-slate-400">
        {label}
        {sub ? <span className="text-slate-300"> · {sub}</span> : null}
      </div>
      <Sparkline values={series} stroke={stroke} />
    </div>
  )
}

const EMPTY: Stats = {
  dataplane: 'detached',
  rx_pps: 0,
  tx_pps: 0,
  dropped_pps: 0,
  new_flows_ps: 0,
  active_flows: 0,
}

interface StatsBarProps {
  stats: Stats | null
  history: Stats[]
}

export function StatsBar({ stats, history }: StatsBarProps) {
  const s = stats ?? EMPTY
  const series = (pick: (x: Stats) => number) => history.map(pick)
  const total = s.rx_pps + s.dropped_pps
  const dropPct = total > 0 ? (s.dropped_pps / total) * 100 : 0

  return (
    <div className="grid grid-cols-2 gap-3 border-b border-slate-200 px-6 py-4 sm:grid-cols-3 lg:grid-cols-5">
      <Card
        label="RX packets/s"
        value={s.rx_pps}
        color="text-sky-500"
        stroke="#0ea5e9"
        series={series((x) => x.rx_pps)}
      />
      <Card
        label="TX packets/s"
        value={s.tx_pps}
        color="text-indigo-500"
        stroke="#6366f1"
        series={series((x) => x.tx_pps)}
      />
      <Card
        label="Active flows"
        value={s.active_flows}
        color="text-amber-500"
        stroke="#f59e0b"
        series={series((x) => x.active_flows)}
      />
      <Card
        label="New flows/s"
        value={s.new_flows_ps}
        color="text-emerald-500"
        stroke="#10b981"
        series={series((x) => x.new_flows_ps)}
      />
      <Card
        label="Dropped/s"
        value={s.dropped_pps}
        color="text-red-500"
        stroke="#ef4444"
        series={series((x) => x.dropped_pps)}
        sub={`${dropPct.toFixed(1)}%`}
        alert={s.dropped_pps > 0}
      />
    </div>
  )
}
