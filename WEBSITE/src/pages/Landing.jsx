import Navbar from '../components/marketing/Navbar.jsx'
import Hero from '../components/marketing/Hero.jsx'
import Features from '../components/marketing/Features.jsx'
import Operations from '../components/marketing/Operations.jsx'
import AiShowcase from '../components/marketing/AiShowcase.jsx'
import Installation from '../components/marketing/Installation.jsx'
import Footer from '../components/marketing/Footer.jsx'

export default function Landing() {
  return (
    <div className="marketing-site">
      <Navbar />
      <main>
        <Hero />
        <Features />
        <Operations />
        <AiShowcase />
        <Installation />
        <Footer />
      </main>
    </div>
  )
}