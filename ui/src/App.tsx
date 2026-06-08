import { useEffect, useState } from 'react'
import { Navbar } from './components/Navbar'
import { StatsBar } from './components/StatsBar'
import { BackendsTable } from './components/BackendsTable'
import type { Stats, BackendsResponse } from './types'

const POLL_INTERVAL_MS = 1000
const HISTORY_LEN = 60

export default function App() {
  const [stats, setStats] = useState<Stats | null>(null)
  const [history, setHistory] = useState<Stats[]>([])
  const [backends, setBackends] = useState<BackendsResponse | null>(null)
  const [online, setOnline] = useState(false)

  useEffect(() => {
    let cancelled = false

    const poll = async () => {
      try {
        const [statsRes, backendsRes] = await Promise.all([
          fetch('/api/v1/stats'),
          fetch('/api/v1/backends'),
        ])
        if (!statsRes.ok || !backendsRes.ok) {
          throw new Error('bad response')
        }
        const statsJson: Stats = await statsRes.json()
        const backendsJson: BackendsResponse = await backendsRes.json()
        if (!cancelled) {
          setStats(statsJson)
          setHistory((h) => [...h, statsJson].slice(-HISTORY_LEN))
          setBackends(backendsJson)
          setOnline(true)
        }
      } catch {
        if (!cancelled) {
          setOnline(false)
        }
      }
    }

    void poll()
    const id = setInterval(() => void poll(), POLL_INTERVAL_MS)
    return () => {
      cancelled = true
      clearInterval(id)
    }
  }, [])

  return (
    <div className="min-h-screen bg-slate-50 font-sans">
      <Navbar
        online={online}
        dataplane={online ? (stats?.dataplane ?? 'detached') : null}
      />
      <StatsBar stats={stats} history={history} />
      <BackendsTable data={backends} />
    </div>
  )
}
