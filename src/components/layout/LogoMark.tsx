import { cn } from '@/lib/utils'

/** Brand mark: three angled green bars forming a stylized road. */
export default function LogoMark({ className }: { className?: string }) {
  return (
    <div
      aria-hidden="true"
      className={cn(
        'flex h-14 w-14 items-end justify-center gap-1.5 rounded-2xl border border-border bg-primary/10 shadow-[0_16px_40px_rgb(74_222_128/15%)]',
        className,
      )}
    >
      <span className="mb-4 h-3.5 w-1.5 -rotate-[18deg] rounded-full bg-primary" />
      <span className="mb-4 h-6 w-1.5 rounded-full bg-primary" />
      <span className="mb-4 h-3.5 w-1.5 rotate-[18deg] rounded-full bg-primary" />
    </div>
  )
}