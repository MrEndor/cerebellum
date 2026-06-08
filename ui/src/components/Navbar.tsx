import type { DataplaneState } from '../types'

interface NavbarProps {
  online: boolean
  dataplane: DataplaneState | null
}

function Pill({ className, children }: { className: string; children: string }) {
  return (
    <span className={`rounded-full px-3 py-0.5 text-xs font-medium ${className}`}>
      {children}
    </span>
  )
}

export function Navbar({ online, dataplane }: NavbarProps) {
  return (
    <header className="flex items-center justify-between border-b border-slate-200 bg-white px-6 py-3">
      <div className="flex items-center gap-3">
        <span className="text-lg font-bold text-slate-900">⬡ Cerebellum</span>
        <span className="text-sm text-slate-400">L4 Load Balancer</span>
      </div>
      <div className="flex items-center gap-2 text-sm">
        <span className="mr-1 text-slate-500">DPDK · AF_XDP</span>
        {!online ? (
          <Pill className="bg-red-100 text-red-800">● Offline</Pill>
        ) : (
          <>
            <Pill className="bg-green-100 text-green-800">● Controlplane</Pill>
            {dataplane === 'connected' ? (
              <Pill className="bg-green-100 text-green-800">● Dataplane</Pill>
            ) : (
              <Pill className="bg-amber-100 text-amber-800">
                ● Dataplane detached
              </Pill>
            )}
          </>
        )}
      </div>
    </header>
  )
}
