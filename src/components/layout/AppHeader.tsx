import { ArrowLeft } from 'lucide-react'
import type { ReactNode } from 'react'

import { Button } from '@/components/ui/button'
import { Separator } from '@/components/ui/separator'

interface AppHeaderProps {
  projectName: string
  subtitle?: string
  onBack: () => void
  /** Right-aligned slot, e.g. a view mode toggle */
  children?: ReactNode
}

export default function AppHeader({ projectName, subtitle, onBack, children }: AppHeaderProps) {
  return (
    <header className="flex h-12 shrink-0 items-center gap-3 border-b border-border bg-card/70 px-3 backdrop-blur">
      <Button variant="ghost" size="sm" onClick={onBack}>
        <ArrowLeft className="size-4" />
        Projects
      </Button>
      <Separator orientation="vertical" className="h-5" />
      <div className="flex min-w-0 items-baseline gap-2">
        <span className="truncate text-sm font-semibold text-foreground">{projectName}</span>
        {subtitle && <span className="truncate text-xs text-muted-foreground">{subtitle}</span>}
      </div>
      <div className="ml-auto flex items-center gap-2">{children}</div>
    </header>
  )
}