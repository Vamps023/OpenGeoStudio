import * as React from 'react'
import { cn } from '@/lib/utils'

// Minimal checkbox (styled native input) — avoids a radix dependency.
const Checkbox = React.forwardRef<HTMLInputElement, React.InputHTMLAttributes<HTMLInputElement>>(
  ({ className, ...props }, ref) => (
    <input
      ref={ref}
      type="checkbox"
      className={cn(
        'size-3.5 shrink-0 cursor-pointer appearance-none rounded-[4px] border border-primary shadow',
        'checked:bg-primary checked:text-primary-foreground',
        className,
      )}
      {...props}
    />
  ),
)
Checkbox.displayName = 'Checkbox'

export { Checkbox }
