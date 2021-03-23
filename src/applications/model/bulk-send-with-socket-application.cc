/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2010 Georgia Institute of Technology
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: George F. Riley <riley@ece.gatech.edu>
 */

#include "ns3/log.h"
#include "ns3/address.h"
#include "ns3/node.h"
#include "ns3/nstime.h"
#include "ns3/socket.h"
#include "ns3/simulator.h"
#include "ns3/socket-factory.h"
#include "ns3/packet.h"
#include "ns3/uinteger.h"
#include "ns3/trace-source-accessor.h"
#include "ns3/tcp-socket-factory.h"
#include "bulk-send-with-socket-application.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("BulkSendWithSocketApplication");

NS_OBJECT_ENSURE_REGISTERED (BulkSendWithSocketApplication);

TypeId
BulkSendWithSocketApplication::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::BulkSendWithSocketApplication")
    .SetParent<Application> ()
    .SetGroupName("Applications") 
    .AddConstructor<BulkSendWithSocketApplication> ()
    .AddAttribute ("SendSize", "The amount of data to send each time.",
                   UintegerValue (512),
                   MakeUintegerAccessor (&BulkSendWithSocketApplication::m_sendSize),
                   MakeUintegerChecker<uint32_t> (1))
    .AddAttribute ("Remote", "The address of the destination",
                   AddressValue (),
                   MakeAddressAccessor (&BulkSendWithSocketApplication::m_peer),
                   MakeAddressChecker ())
    .AddAttribute ("MaxBytes",
                   "The total number of bytes to send. "
                   "Once these bytes are sent, "
                   "no data  is sent again. The value zero means "
                   "that there is no limit.",
                   UintegerValue (0),
                   MakeUintegerAccessor (&BulkSendWithSocketApplication::m_maxBytes),
                   MakeUintegerChecker<uint64_t> ())
    .AddAttribute ("Protocol", "The type of protocol to use.",
                   TypeIdValue (TcpSocketFactory::GetTypeId ()),
                   MakeTypeIdAccessor (&BulkSendWithSocketApplication::m_tid),
                   MakeTypeIdChecker ())
    .AddTraceSource ("Tx", "A new packet is created and is sent",
                     MakeTraceSourceAccessor (&BulkSendWithSocketApplication::m_txTrace),
                     "ns3::Packet::TracedCallback")
  ;
  return tid;
}


BulkSendWithSocketApplication::BulkSendWithSocketApplication ()
  : m_socket (0),
    m_connected (false),
    m_totBytes (0)
{
  NS_LOG_FUNCTION (this);
}

BulkSendWithSocketApplication::~BulkSendWithSocketApplication ()
{
  NS_LOG_FUNCTION (this);
}

void
BulkSendWithSocketApplication::SetMaxBytes (uint64_t maxBytes)
{
  NS_LOG_FUNCTION (this << maxBytes);
  m_maxBytes = maxBytes;
}

Ptr<Socket>
BulkSendWithSocketApplication::GetSocket (void) const
{
  NS_LOG_FUNCTION (this);
  return m_socket;
}


void
BulkSendWithSocketApplication::Setup(Ptr<Socket> socket, Address peer, uint32_t sendSize) {
  m_socket = socket;
  m_peer = peer;
  m_sendSize = sendSize;
}

void
BulkSendWithSocketApplication::DoDispose (void)
{
  NS_LOG_FUNCTION (this);

  m_socket = 0;
  // chain up
  Application::DoDispose ();
}

// Application Methods
void BulkSendWithSocketApplication::StartApplication (void) // Called at time specified by Start
{
  NS_LOG_FUNCTION (this);

  // Create the socket if not already
  // Fatal error if socket type is not NS3_SOCK_STREAM or NS3_SOCK_SEQPACKET
  if (m_socket->GetSocketType () != Socket::NS3_SOCK_STREAM &&
      m_socket->GetSocketType () != Socket::NS3_SOCK_SEQPACKET)
    {
      NS_FATAL_ERROR ("Using BulkSend with an incompatible socket type. "
                      "BulkSend requires SOCK_STREAM or SOCK_SEQPACKET. "
                      "In other words, use TCP instead of UDP.");
    }

  if (Inet6SocketAddress::IsMatchingType (m_peer))
    {
      if (m_socket->Bind6 () == -1)
        {
          NS_FATAL_ERROR ("Failed to bind socket");
        }
    }
  else if (InetSocketAddress::IsMatchingType (m_peer))
    {
      if (m_socket->Bind () == -1)
        {
          NS_FATAL_ERROR ("Failed to bind socket");
        }
    }

  m_socket->Connect (m_peer);
  m_socket->ShutdownRecv ();
  m_socket->SetConnectCallback (
    MakeCallback (&BulkSendWithSocketApplication::ConnectionSucceeded, this),
    MakeCallback (&BulkSendWithSocketApplication::ConnectionFailed, this));
  m_socket->SetSendCallback (
    MakeCallback (&BulkSendWithSocketApplication::DataSend, this));
  if (m_connected)
    {
      SendData ();
    }
}

void BulkSendWithSocketApplication::StopApplication (void) // Called at time specified by Stop
{
  NS_LOG_FUNCTION (this);

  if (m_socket != 0)
    {
      m_socket->Close ();
      m_connected = false;
    }
  else
    {
      NS_LOG_WARN ("BulkSendWithSocketApplication found null socket to close in StopApplication");
    }
}


// Private helpers

void BulkSendWithSocketApplication::SendData (void)
{
  NS_LOG_FUNCTION (this);

  while (m_maxBytes == 0 || m_totBytes < m_maxBytes)
    { // Time to send more

      // uint64_t to allow the comparison later.
      // the result is in a uint32_t range anyway, because
      // m_sendSize is uint32_t.
      uint64_t toSend = m_sendSize;
      // Make sure we don't send too many
      if (m_maxBytes > 0)
        {
          toSend = std::min (toSend, m_maxBytes - m_totBytes);
        }

      NS_LOG_LOGIC ("sending packet at " << Simulator::Now ());
      Ptr<Packet> packet = Create<Packet> (toSend);
      int actual = m_socket->Send (packet);
      if (actual > 0)
        {
          m_totBytes += actual;
          m_txTrace (packet);
        }
      // We exit this loop when actual < toSend as the send side
      // buffer is full. The "DataSent" callback will pop when
      // some buffer space has freed ip.
      if ((unsigned)actual != toSend)
        {
          break;
        }
    }
  // Check if time to close (all sent)
  if (m_totBytes == m_maxBytes && m_connected)
    {
      m_socket->Close ();
      m_connected = false;
    }
}

void BulkSendWithSocketApplication::ConnectionSucceeded (Ptr<Socket> socket)
{
  NS_LOG_FUNCTION (this << socket);
  NS_LOG_LOGIC ("BulkSendWithSocketApplication Connection succeeded");
  m_connected = true;
  SendData ();
}

void BulkSendWithSocketApplication::ConnectionFailed (Ptr<Socket> socket)
{
  NS_LOG_FUNCTION (this << socket);
  NS_LOG_LOGIC ("BulkSendWithSocketApplication, Connection Failed");
}

void BulkSendWithSocketApplication::DataSend (Ptr<Socket>, uint32_t)
{
  NS_LOG_FUNCTION (this);

  if (m_connected)
    { // Only send new data if the connection has completed
      SendData ();
    }
}



} // Namespace ns3
