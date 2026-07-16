import { Boxes } from 'lucide-react'

export default function Brand({ compact = false }) {
  return (
    <span className="brand" aria-label="MCDeploy">
      <span className="brand-mark"><Boxes size={compact ? 18 : 22} /></span>
      <span>MC<span>Deploy</span></span>
    </span>
  )
}