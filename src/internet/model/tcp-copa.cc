// #include "tcp-copa.h"
#include "ns3/tcp-copa.h"
#include "ns3/simulator.h"
#include "ns3/nstime.h"
#include "ns3/log.h"

namespace ns3
{
    NS_LOG_COMPONENT_DEFINE("TcpCopa");
    NS_OBJECT_ENSURE_REGISTERED(TcpCopa);

    TypeId TcpCopa::GetTypeId(void)
    {
        static TypeId tid = TypeId("ns3::TcpCopa")
                                .SetParent<TcpCongestionOps>()
                                .AddConstructor<TcpCopa>()
                                .SetGroupName("Internet")
                                .AddAttribute("srttUpdateFactor",
                                              "The factor for updating SRTT using exponential weighted moving average",
                                              DoubleValue(0.9),
                                              MakeDoubleAccessor(&TcpCopa::m_srttUpdateFactor),
                                              MakeDoubleChecker<double>())
                                .AddAttribute("srttDefualtValue",
                                              "Default value for srtt",
                                              TimeValue(Time("100ms")),
                                              MakeTimeAccessor(&TcpCopa::m_srtt),
                                              MakeTimeChecker())
                                .AddAttribute("defaultDelta",
                                              "Value of delta in default mode",
                                              DoubleValue(0.5),
                                              MakeDoubleAccessor(&TcpCopa::m_defaultDelta),
                                              MakeDoubleChecker<double>())
                                .AddAttribute("RTTminFilterResetInterval",
                                              "Time interval to update RTTmin",
                                              TimeValue(Time("10s")),
                                              MakeTimeAccessor(&TcpCopa::m_rttMinFilterResetInterval),
                                              MakeTimeChecker())
                                .AddTraceSource("CongestionWindow",
                                                "The TCP connection's congestion window",
                                                MakeTraceSourceAccessor(&TcpCopa::m_cWnd),
                                                "ns3::TracedValueCallback::Uint32")
                                .AddTraceSource("RTTStanding",
                                                "RTTstanding measured each time TCP received an ACK",
                                                MakeTraceSourceAccessor(&TcpCopa::m_rttStanding),
                                                "ns3::TracedValueCallback::Time")
                                .AddTraceSource("TxRate",
                                                "Recent transmit rate",
                                                MakeTraceSourceAccessor(&TcpCopa::m_txRate),
                                                "ns3::TracedValueCallback::double")
                                .AddTraceSource("QueueDelayThresh",
                                                "The thresh for model switching",
                                                MakeTraceSourceAccessor(&TcpCopa::m_queueDelayThresh),
                                                "ns3::TracedValueCallback::Uint32");
        return tid;
    }

    TcpCopa::TcpCopa() : TcpCongestionOps(),
                         m_rttMinFilter(m_rttMinFilterResetInterval.GetInteger(), Time(0), 0),
                         m_rttStandingFilterBase(m_srtt.GetInteger() / 2, Time(0), 0) {}

    TcpCopa::TcpCopa(const TcpCopa &sock) : TcpCongestionOps(sock),
                                            m_rttMinFilter(m_rttMinFilterResetInterval.GetInteger(), Time(0), 0),
                                            m_rttStandingFilterBase(m_srtt.GetInteger() / 2, Time(0), 0),
                                            m_velocityState(sock.m_velocityState),
                                            m_modeState(sock.m_modeState) {}

    TcpCopa::~TcpCopa() {}

    std::string TcpCopa::GetName() const
    {
        return "TcpCopa";
    }

    uint32_t TcpCopa::GetSsThresh(Ptr<const TcpSocketState> tcb,
                         uint32_t bytesInFlight)
    {
        return m_cWnd.Get();
    }

    void TcpCopa::CongestionStateSet(Ptr<TcpSocketState> tcb, const TcpSocketState::TcpCongState_t newState)
    {
        if (ModeDetector() == CompetingMode && newState == TcpSocketState::CA_RECOVERY)
        {
            m_deltaThresh = m_delta * 2;
        }
    }

    void TcpCopa::CwndEvent(Ptr<TcpSocketState> tcb, const TcpSocketState::TcpCAEvent_t event)
    {
        if (ModeDetector() == CompetingMode)
        {
            if (event == TcpSocketState::CA_EVENT_LOSS)
            {
                m_deltaThresh = m_delta * 2;
                m_delta = m_defaultDelta;
            }
            else if (event == TcpSocketState::CA_EVENT_COMPLETE_CWR)
            {
                m_delta = m_deltaThresh;
            }
        }
    }

    void TcpCopa::PktsAcked(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked, const Time &rtt)
    {
        UpdateRttStanding(rtt, Simulator::Now());
        UpdateRttMin(rtt, Simulator::Now());
        UpdateSrtt(rtt);
        UpdateQueueDelay();
        UpdateTargetTxRate();
        UpdateCwndAndTxRate(tcb->m_cWnd);
        AdjustVelocityState(Simulator::Now());
        ModeSwitch(rtt, Simulator::Now());
    }

    void TcpCopa::IncreaseWindow(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked)
    {
        switch (ModeDetector())
        {
        case StartMode:
            m_cWnd = tcb->m_cWnd = 2 * m_cWnd;
            break;

        case DefaultMode:
            AdjustCwnd();
            break;

        case CompetingMode:

            AdjustCwnd();
            break;

        default:
            break;
        }
    }

    void TcpCopa::UpdateRttStanding(Time newRtt, Time currentTime)
    {
        if (currentTime - m_rttStandingFilterResetTime > m_srtt / 2)
        {
            m_rttStandingFilterBase.SetWindowLength(m_srtt.GetInteger() / 2);
            m_rttStandingFilterResetTime = currentTime;
        }
        m_rttStandingFilterBase.Update(newRtt, currentTime.GetTimeStep());
        m_rttStanding = m_rttStandingFilterBase.GetBest();
    }

    inline void TcpCopa::UpdateSrtt(Time newRtt)
    {
        m_srtt = Time(m_srttUpdateFactor * m_srtt.GetInteger() +
                      (1 - m_srttUpdateFactor) * newRtt.GetInteger());
    }

    inline void TcpCopa::UpdateRttMin(Time newRtt, Time currentTime)
    {
        m_rttMinFilter.Update(newRtt, currentTime.GetTimeStep());
        m_rttMin = m_rttMinFilter.GetBest();
    }

    inline void TcpCopa::UpdateQueueDelay()
    {
        m_queueDelay = m_rttStanding.Get().GetInteger() - m_rttMin.GetInteger();
    }

    inline void TcpCopa::UpdateTargetTxRate()
    {
        m_targetTxRate = 1 / (m_delta * m_queueDelay);
    }

    inline void TcpCopa::UpdateCwndAndTxRate(uint32_t currentCWnd)
    {
        m_cWnd = currentCWnd;
        m_txRate.Set(m_cWnd.Get() / double(m_rttStanding.Get().GetInteger()));
    }

    void TcpCopa::ModeSwitch(Time newRtt, Time currentTime)
    {
        // update mode state parameters
        if (currentTime - m_modeState.cycleStartTime <= 4 * m_srtt)
        {
            if (newRtt > m_modeState.rttMax)
                m_modeState.rttMax = newRtt;
        }
        else
        {
            if (m_rttStanding - m_rttMin < m_modeState.queueDelayMin)
            {
                m_modeState.queueDelayMin = m_rttStanding - m_rttMin;
            }
        }

        // start a new update cycle
        if (currentTime > m_modeState.cycleStartTime + 5 * m_srtt)
        {
            if (m_modeState.queueDelayMin <
                0.1 * (m_modeState.rttMax.GetInteger() - m_rttMin.GetInteger()))
            {
                m_modeState.isCompeting = false;
            }
            else
                m_modeState.isCompeting = true;
            m_modeState.cycleStartTime = currentTime;
            m_modeState.rttMax = m_modeState.queueDelayMin = Time(0);
        }

        // update mode
        if (m_modeState.mode == StartMode && m_txRate >= m_targetTxRate)
        {
            if (!m_modeState.isCompeting)
            {
                m_modeState.mode = DefaultMode;
            }
            else
                m_modeState.mode = CompetingMode;
        }
        else if (m_modeState.mode == DefaultMode && m_modeState.isCompeting)
        {
            m_modeState.mode = CompetingMode;
        }
        else if (m_modeState.mode == CompetingMode && !m_modeState.isCompeting)
        {
            m_delta = m_defaultDelta;
            m_modeState.mode = DefaultMode;
        }
    }

    inline TcpCopa::Mode TcpCopa::ModeDetector()
    {
        return m_modeState.mode;
    }

    inline void TcpCopa::AdjustCwnd()
    {
        if (m_txRate <= m_targetTxRate)
            m_cWnd += m_velocityState.velocity / (m_delta * m_cWnd);
        else
            m_cWnd += m_velocityState.velocity / (m_delta * m_cWnd);
    }

    void TcpCopa::AdjustVelocityState(Time currentTime)
    {
        if (currentTime - m_velocityState.lastRecordTime >= m_srtt)
        {
            Direction newDirection;
            if (m_cWnd > m_velocityState.lastRecordedCwnd)
                newDirection = Up;
            else
                newDirection = Down;
            m_velocityState.lastRecordTime = currentTime;
            m_velocityState.lastRecordedCwnd = m_cWnd;
            if (newDirection == m_velocityState.direction)
            {
                m_velocityState.numTimesDirectionSame++;
                if (m_velocityState.numTimesDirectionSame >= 2)
                {
                    m_velocityState.velocity = 2 * m_velocityState.velocity;
                    m_velocityState.numTimesDirectionSame = -1;
                }
            }
            else
            {
                m_velocityState.direction = newDirection;
                m_velocityState.velocity = 1;
                m_velocityState.numTimesDirectionSame = 0;
            }
        }
    }

    inline void TcpCopa::AdjustDelta()
    {
        if (m_delta < m_deltaThresh)
            m_delta = std::max(m_delta / 2, m_deltaThresh);
        else
            m_delta = 1 / (1 / m_delta + 1);
    }

    Ptr<TcpCongestionOps> TcpCopa::Fork()
    {
        return CopyObject<TcpCopa> (this);
    }
}