/*
 * Copyright (c) 2003-2018 Ke Hengzhong <kehengzhong@hotmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice, 
 *    this list of conditions and the following disclaimer in the documentation 
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MER-
 * CHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO
 * EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPE-
 * CIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTH-
 * ERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "epm_util.h"
#include "epm_sock.h"

#include "epcore.h"
#include "epump_internal.h"
#include "iodev.h"
#include "eprawsock.h"


void * eprawsock_client (void * vpcore, void * para, int protocol, int * retval)
{
    epcore_t * pcore = (epcore_t *)vpcore;
    iodev_t  * pdev = NULL;
    int        ret = 0;
    uint32     bOpt = 1;
    int        ntimeout = 2000;
 
    if (retval) *retval = -1;
    if (!pcore) return NULL;
 
    pdev = iodev_new(pcore);
    if (!pdev) {
        if (retval) *retval = -100;
        return NULL;
    }
 
#ifdef _WIN32
    pdev->fd = WSASocket(AF_INET, SOCK_RAW, /*IPPROTO_UDP*/protocol,
                        NULL, 0, WSA_FLAG_OVERLAPPED);
#else
    pdev->fd = socket(AF_INET, SOCK_RAW, /*IPPROTO_UDP*/protocol);
#endif
    if (pdev->fd == INVALID_SOCKET) {
        iodev_close(pdev);
        if (retval) *retval = -200;
        return NULL;
    }
 
    bOpt = 1;
    ret = setsockopt(pdev->fd, IPPROTO_IP, IP_HDRINCL, (char *)&bOpt, sizeof(bOpt));
    if (ret == SOCKET_ERROR) {
        ret = WSAGetLastError();
        iodev_close(pdev);
        if (retval) *retval = -300;
        return NULL;
    }
 
    ntimeout = 1000;
    ret = setsockopt(pdev->fd, SOL_SOCKET, SO_SNDTIMEO, (char *)&ntimeout, sizeof(ntimeout));
    if (ret == SOCKET_ERROR) {
        ret = WSAGetLastError();
        iodev_close(pdev);
        if (retval) *retval = -400;
        return NULL;
    }
 
    ntimeout = 1000;
    ret = setsockopt(pdev->fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&ntimeout, sizeof(ntimeout));
    if (ret == SOCKET_ERROR) {
        ret = WSAGetLastError();
        iodev_close(pdev);
        if (retval) *retval = -400;
        return NULL;
    }
 
    epm_sock_nonblock_set(pdev->fd, 1);

    pdev->para = para;
    pdev->callback = NULL;
    pdev->cbpara = NULL;
 
    pdev->iostate = IOS_READWRITE;
    pdev->fdtype = FDT_RAWSOCK;
 
    iodev_rwflag_set(pdev, RWF_READ);

    if (retval) *retval = 0;
    return pdev;
}
 
int eprawsock_notify (void * vpdev, int recvall, IOHandler * cb, void * cbpara)
{
    iodev_t  * pdev = (iodev_t *)vpdev;
#ifdef _WIN32
    DWORD      lpvBuffer = 1;
    DWORD      lpcbBytesReturned = 0;
#endif
 
    if (!pdev) return -1;
 
    if (recvall) {
    #ifdef _WIN32
        WSAIoctl(pdev->fd, SIO_RCVALL, &lpvBuffer,
            sizeof(lpvBuffer), NULL, 0, &lpcbBytesReturned, NULL, NULL);
    #endif
    }
 
    pdev->callback = cb;
    pdev->cbpara = cbpara;
 
    return 0;
}
 

static uint16 rfc_checksum (uint16 * buffer, int size)
{
    uint32 cksum=0;

    while (size > 1) {
        cksum += *buffer++;
        size -= sizeof(uint16);
    }
    if (size) {
        cksum += *(uint8*)buffer;
    }

    cksum = (cksum >> 16) + (cksum & 0xffff);
    cksum += (cksum >>16);

    return (uint16)(~cksum);
}

 
int eprawsock_send_udp (void * vpdev, char * srcip, uint16 srcport, char * dstip,
                        uint16 dstport, char * pbyte, int bytelen)
{
    iodev_t  * pdev = (iodev_t *)vpdev;
    IP_HDR     ipHdr;
    UDP_HDR    udpHdr;
    int        ret;
    int        iTotalSize = 0;
    uint16     iIPVersion = 0;
    char       phdr[4096];
    int        hdrlen = 0;
    struct sockaddr_in  remote;
 
    if (!vpdev) return -1;
 
    iTotalSize = sizeof(ipHdr) + sizeof(udpHdr) + bytelen;
 
    iIPVersion = 4;
 
    ipHdr.ip_verlen = (iIPVersion << 4) | (sizeof(ipHdr)/4);
    ipHdr.ip_tos = 0;
    ipHdr.ip_totallength = htons((uint16)iTotalSize);
    ipHdr.ip_id = 0;
    ipHdr.ip_offset = 0x0040;
    ipHdr.ip_ttl = 250;
    ipHdr.ip_protocol = 0x11; //UDP over IP
    ipHdr.ip_checksum = 0 ;
    ipHdr.ip_srcaddr = inet_addr(srcip);
    ipHdr.ip_destaddr = inet_addr(dstip);
 
    udpHdr.src_portno = htons(srcport) ;
    udpHdr.dst_portno = htons(dstport) ;
    udpHdr.udp_length = htons((uint16)(sizeof(udpHdr) + bytelen)) ;
    udpHdr.udp_checksum = 0 ;
 
    memcpy(phdr+hdrlen, (char *)&ipHdr.ip_srcaddr, sizeof(ipHdr.ip_srcaddr));
    hdrlen += sizeof(ipHdr.ip_srcaddr);
    memcpy(phdr+hdrlen, (char *)&ipHdr.ip_destaddr, sizeof(ipHdr.ip_destaddr));
    hdrlen += sizeof(ipHdr.ip_destaddr);
    phdr[hdrlen++] = 0x00;
    memcpy(phdr+hdrlen, (char *)&ipHdr.ip_protocol, sizeof(ipHdr.ip_protocol));
    hdrlen += sizeof(ipHdr.ip_protocol);
    memcpy(phdr+hdrlen, (char *)&udpHdr.udp_length, sizeof(udpHdr.udp_length));
    hdrlen += sizeof(udpHdr.udp_length);
    memcpy(phdr+hdrlen, (char *)&udpHdr, sizeof(udpHdr));
    hdrlen += sizeof(udpHdr);
    memcpy(phdr+hdrlen, (char *)pbyte, bytelen);
    hdrlen += bytelen;
    udpHdr.udp_checksum = rfc_checksum((uint16 *)phdr, hdrlen);
 
    hdrlen = 0;
    memcpy(phdr+hdrlen, (char *)&ipHdr, sizeof(ipHdr));
    hdrlen += sizeof(ipHdr);
    memcpy(phdr+hdrlen, (char *)&udpHdr, sizeof(udpHdr));
    hdrlen += sizeof(udpHdr);
    memcpy(phdr+hdrlen, (char *)pbyte, bytelen);
    hdrlen += bytelen;
 
    remote.sin_family = AF_INET;
    remote.sin_port = htons(dstport);
    remote.sin_addr.s_addr = inet_addr(dstip);
 
    EnterCriticalSection(&pdev->fdCS);
    ret = sendto(pdev->fd, phdr, iTotalSize, 0, (struct sockaddr *)&remote, sizeof(remote));
    LeaveCriticalSection(&pdev->fdCS);
    if (ret == SOCKET_ERROR) {
        ret = WSAGetLastError();
        return -100;
    }
 
    return 0;
}
 
 
int eprawsock_send_icmp (void * vpdev, char * srcip, char * dstip, uint8 icmptype,
                         uint16 icmpid, uint16 icmpseq, char * pbyte, int bytelen)
{
    iodev_t  * pdev = (iodev_t *)vpdev;
    IP_HDR     ipHdr;
    ICMP_HDR   icmpHdr;
    int        ret;
    int        iTotalSize = 0;
    uint16     iIPVersion = 0;
    char       phdr[4096];
    int        hdrlen = 0;
    epm_time_t curt;
    struct sockaddr_in  remote;
 
    if (!vpdev) return -1;
 
    iTotalSize = sizeof(ipHdr) + sizeof(icmpHdr) + bytelen;
 
    iIPVersion = 4;
 
    ipHdr.ip_verlen = (iIPVersion << 4) | (sizeof(ipHdr)/4);
    ipHdr.ip_tos = 0;
    ipHdr.ip_totallength = htons((uint16)iTotalSize);
    ipHdr.ip_id = 0;
    ipHdr.ip_offset = 0x0040;
    ipHdr.ip_ttl = 255;
    ipHdr.ip_protocol = 0x01; //ICMP over IP
    ipHdr.ip_checksum = 0 ;
    ipHdr.ip_srcaddr = inet_addr(srcip);
    ipHdr.ip_destaddr = inet_addr(dstip);
 
    epm_time(&curt);
 
    icmpHdr.icmp_type = icmptype;
    icmpHdr.icmp_code = 0;
    icmpHdr.icmp_checksum = 0;
    icmpHdr.icmp_id = icmpid;
    icmpHdr.icmp_seq = icmpseq;
    icmpHdr.icmp_timestamp = curt.s*1000 + curt.ms;
 
    hdrlen = 0;
    memcpy(phdr+hdrlen, (char *)&icmpHdr, sizeof(icmpHdr)); hdrlen += sizeof(icmpHdr);
    memcpy(phdr+hdrlen, (char *)pbyte, bytelen);  hdrlen += bytelen;
    icmpHdr.icmp_checksum = rfc_checksum((uint16 *)phdr, hdrlen);
 
    hdrlen = 0;
    memcpy(phdr+hdrlen, (char *)&ipHdr, sizeof(ipHdr));  hdrlen += sizeof(ipHdr);
    memcpy(phdr+hdrlen, (char *)&icmpHdr, sizeof(icmpHdr)); hdrlen += sizeof(icmpHdr);
    memcpy(phdr+hdrlen, (char *)pbyte, bytelen); hdrlen += bytelen;
 
    remote.sin_family = AF_INET;
    remote.sin_port = 0;
    remote.sin_addr.s_addr = inet_addr(dstip);
 
    EnterCriticalSection(&pdev->fdCS);
    ret = sendto(pdev->fd, phdr, iTotalSize, 0, (struct sockaddr *)&remote, sizeof(remote));
    LeaveCriticalSection(&pdev->fdCS);
    if (ret == SOCKET_ERROR) {
        ret = WSAGetLastError();
        return -100;
    }
 
    return 0;
}

