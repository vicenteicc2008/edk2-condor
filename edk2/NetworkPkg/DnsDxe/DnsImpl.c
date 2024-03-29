/** @file
DnsDxe support functions implementation.
  
Copyright (c) 2016, Intel Corporation. All rights reserved.<BR>
This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#include "DnsImpl.h"

/**
  Remove TokenEntry from TokenMap.

  @param[in] TokenMap          All DNSv4 Token entrys.
  @param[in] TokenEntry        TokenEntry need to be removed.

  @retval EFI_SUCCESS          Remove TokenEntry from TokenMap sucessfully.
  @retval EFI_NOT_FOUND        TokenEntry is not found in TokenMap.

**/
EFI_STATUS
Dns4RemoveTokenEntry (
  IN NET_MAP                    *TokenMap,
  IN DNS4_TOKEN_ENTRY           *TokenEntry
  )
{
  NET_MAP_ITEM  *Item;

  //
  // Find the TokenEntry first.
  //
  Item = NetMapFindKey (TokenMap, (VOID *) TokenEntry);

  if (Item != NULL) {
    //
    // Remove the TokenEntry if it's found in the map.
    //
    NetMapRemoveItem (TokenMap, Item, NULL);

    return EFI_SUCCESS;
  }
  
  return EFI_NOT_FOUND;
}

/**
  Remove TokenEntry from TokenMap.

  @param[in] TokenMap           All DNSv6 Token entrys.
  @param[in] TokenEntry         TokenEntry need to be removed.

  @retval EFI_SUCCESS           Remove TokenEntry from TokenMap sucessfully.
  @retval EFI_NOT_FOUND         TokenEntry is not found in TokenMap.
  
**/
EFI_STATUS
Dns6RemoveTokenEntry (
  IN NET_MAP                    *TokenMap,
  IN DNS6_TOKEN_ENTRY           *TokenEntry
  )
{
  NET_MAP_ITEM  *Item;

  //
  // Find the TokenEntry first.
  //
  Item = NetMapFindKey (TokenMap, (VOID *) TokenEntry);

  if (Item != NULL) {
    //
    // Remove the TokenEntry if it's found in the map.
    //
    NetMapRemoveItem (TokenMap, Item, NULL);

    return EFI_SUCCESS;
  }
  
  return EFI_NOT_FOUND;
}

/**
  This function cancle the token specified by Arg in the Map.

  @param[in]  Map             Pointer to the NET_MAP.
  @param[in]  Item            Pointer to the NET_MAP_ITEM.
  @param[in]  Arg             Pointer to the token to be cancelled. If NULL, all
                              the tokens in this Map will be cancelled.
                              This parameter is optional and may be NULL.

  @retval EFI_SUCCESS         The token is cancelled if Arg is NULL, or the token
                              is not the same as that in the Item, if Arg is not
                              NULL.
  @retval EFI_ABORTED         Arg is not NULL, and the token specified by Arg is
                              cancelled.

**/
EFI_STATUS
EFIAPI
Dns4CancelTokens (
  IN NET_MAP       *Map,
  IN NET_MAP_ITEM  *Item,
  IN VOID          *Arg OPTIONAL
  )
{
  DNS4_TOKEN_ENTRY           *TokenEntry;
  NET_BUF                    *Packet;
  UDP_IO                     *UdpIo;

  if ((Arg != NULL) && (Item->Key != Arg)) {
    return EFI_SUCCESS;
  }

  if (Item->Value != NULL) {
    //
    // If the TokenEntry is a transmit TokenEntry, the corresponding Packet is recorded in
    // Item->Value.
    //
    Packet  = (NET_BUF *) (Item->Value);
    UdpIo = (UDP_IO *) (*((UINTN *) &Packet->ProtoData[0]));

    UdpIoCancelSentDatagram (UdpIo, Packet);
  }

  //
  // Remove TokenEntry from Dns4TxTokens.
  //
  TokenEntry = (DNS4_TOKEN_ENTRY *) Item->Key;
  if (Dns4RemoveTokenEntry (Map, TokenEntry) == EFI_SUCCESS) {
    TokenEntry->Token->Status = EFI_ABORTED;
    gBS->SignalEvent (TokenEntry->Token->Event);
    DispatchDpc ();
  }

  if (Arg != NULL) {
    return EFI_ABORTED;
  }

  return EFI_SUCCESS;
}

/**
  This function cancle the token specified by Arg in the Map.

  @param[in]  Map             Pointer to the NET_MAP.
  @param[in]  Item            Pointer to the NET_MAP_ITEM.
  @param[in]  Arg             Pointer to the token to be cancelled. If NULL, all
                              the tokens in this Map will be cancelled.
                              This parameter is optional and may be NULL.

  @retval EFI_SUCCESS         The token is cancelled if Arg is NULL, or the token
                              is not the same as that in the Item, if Arg is not
                              NULL.
  @retval EFI_ABORTED         Arg is not NULL, and the token specified by Arg is
                              cancelled.

**/
EFI_STATUS
EFIAPI
Dns6CancelTokens (
  IN NET_MAP       *Map,
  IN NET_MAP_ITEM  *Item,
  IN VOID          *Arg OPTIONAL
  )
{
  DNS6_TOKEN_ENTRY           *TokenEntry;
  NET_BUF                    *Packet;
  UDP_IO                     *UdpIo;

  if ((Arg != NULL) && (Item->Key != Arg)) {
    return EFI_SUCCESS;
  }

  if (Item->Value != NULL) {
    //
    // If the TokenEntry is a transmit TokenEntry, the corresponding Packet is recorded in
    // Item->Value.
    //
    Packet  = (NET_BUF *) (Item->Value);
    UdpIo = (UDP_IO *) (*((UINTN *) &Packet->ProtoData[0]));

    UdpIoCancelSentDatagram (UdpIo, Packet);
  }

  //
  // Remove TokenEntry from Dns6TxTokens.
  //
  TokenEntry = (DNS6_TOKEN_ENTRY *) Item->Key;
  if (Dns6RemoveTokenEntry (Map, TokenEntry) == EFI_SUCCESS) {
    TokenEntry->Token->Status = EFI_ABORTED;
    gBS->SignalEvent (TokenEntry->Token->Event);
    DispatchDpc ();
  }

  if (Arg != NULL) {
    return EFI_ABORTED;
  }

  return EFI_SUCCESS;
}

/**
  Get the TokenEntry from the TokensMap.

  @param[in]  TokensMap           All DNSv4 Token entrys
  @param[in]  Token               Pointer to the token to be get.
  @param[out] TokenEntry          Pointer to TokenEntry corresponding Token.

  @retval EFI_SUCCESS             Get the TokenEntry from the TokensMap sucessfully.
  @retval EFI_NOT_FOUND           TokenEntry is not found in TokenMap.

**/
EFI_STATUS
EFIAPI
GetDns4TokenEntry (
  IN     NET_MAP                   *TokensMap,
  IN     EFI_DNS4_COMPLETION_TOKEN *Token, 
     OUT DNS4_TOKEN_ENTRY          **TokenEntry
  )
{
  LIST_ENTRY              *Entry;
  
  NET_MAP_ITEM            *Item;
  
  NET_LIST_FOR_EACH (Entry, &TokensMap->Used) {
    Item = NET_LIST_USER_STRUCT (Entry, NET_MAP_ITEM, Link);
    *TokenEntry = (DNS4_TOKEN_ENTRY *) (Item->Key);  
    if ((*TokenEntry)->Token == Token) {
      return EFI_SUCCESS;
    }
  }
  
  *TokenEntry = NULL;
  
  return EFI_NOT_FOUND;
}

/**
  Get the TokenEntry from the TokensMap.

  @param[in]  TokensMap           All DNSv6 Token entrys
  @param[in]  Token               Pointer to the token to be get.
  @param[out] TokenEntry          Pointer to TokenEntry corresponding Token.

  @retval EFI_SUCCESS             Get the TokenEntry from the TokensMap sucessfully.
  @retval EFI_NOT_FOUND           TokenEntry is not found in TokenMap.

**/
EFI_STATUS
EFIAPI
GetDns6TokenEntry (
  IN     NET_MAP                   *TokensMap,
  IN     EFI_DNS6_COMPLETION_TOKEN *Token, 
     OUT DNS6_TOKEN_ENTRY          **TokenEntry
  )
{
  LIST_ENTRY              *Entry;
  
  NET_MAP_ITEM            *Item;
  
  NET_LIST_FOR_EACH (Entry, &TokensMap->Used) {
    Item = NET_LIST_USER_STRUCT (Entry, NET_MAP_ITEM, Link);
    *TokenEntry = (DNS6_TOKEN_ENTRY *) (Item->Key);  
    if ((*TokenEntry)->Token == Token) {
      return EFI_SUCCESS;
    }
  }
  
  *TokenEntry =NULL;
  
  return EFI_NOT_FOUND;
}

/**
  Cancel DNS4 tokens from the DNS4 instance.

  @param[in]  Instance           Pointer to the DNS instance context data.
  @param[in]  Token              Pointer to the token to be canceled. If NULL, all
                                 tokens in this instance will be cancelled.
                                 This parameter is optional and may be NULL.

  @retval EFI_SUCCESS            The Token is cancelled.
  @retval EFI_NOT_FOUND          The Token is not found.

**/
EFI_STATUS
Dns4InstanceCancelToken (
  IN DNS_INSTANCE               *Instance,
  IN EFI_DNS4_COMPLETION_TOKEN  *Token
  )
{
  EFI_STATUS        Status;
  DNS4_TOKEN_ENTRY  *TokenEntry;

  TokenEntry = NULL;

  if(Token != NULL  ) {
    Status = GetDns4TokenEntry (&Instance->Dns4TxTokens, Token, &TokenEntry);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  } else {
    TokenEntry = NULL;
  }

  //
  // Cancel this TokenEntry from the Dns4TxTokens map.
  //
  Status = NetMapIterate (&Instance->Dns4TxTokens, Dns4CancelTokens, TokenEntry);

  if ((TokenEntry != NULL) && (Status == EFI_ABORTED)) {
    //
    // If Token isn't NULL and Status is EFI_ABORTED, the token is cancelled from
    // the Dns4TxTokens and returns success.
    //
    if (NetMapIsEmpty (&Instance->Dns4TxTokens)) {
       Instance->UdpIo->Protocol.Udp4->Cancel (Instance->UdpIo->Protocol.Udp4, &Instance->UdpIo->RecvRequest->Token.Udp4);
    }
    return EFI_SUCCESS;
  }

  ASSERT ((TokenEntry != NULL) || (0 == NetMapGetCount (&Instance->Dns4TxTokens)));
  
  if (NetMapIsEmpty (&Instance->Dns4TxTokens)) {
    Instance->UdpIo->Protocol.Udp4->Cancel (Instance->UdpIo->Protocol.Udp4, &Instance->UdpIo->RecvRequest->Token.Udp4);
  }

  return EFI_SUCCESS;
}

/**
  Cancel DNS6 tokens from the DNS6 instance.

  @param[in]  Instance           Pointer to the DNS instance context data.
  @param[in]  Token              Pointer to the token to be canceled. If NULL, all
                                 tokens in this instance will be cancelled.
                                 This parameter is optional and may be NULL.

  @retval EFI_SUCCESS            The Token is cancelled.
  @retval EFI_NOT_FOUND          The Token is not found.

**/
EFI_STATUS
Dns6InstanceCancelToken (
  IN DNS_INSTANCE               *Instance,
  IN EFI_DNS6_COMPLETION_TOKEN  *Token
  )
{
  EFI_STATUS        Status;
  DNS6_TOKEN_ENTRY  *TokenEntry;

  TokenEntry = NULL;

  if(Token != NULL  ) {
    Status = GetDns6TokenEntry (&Instance->Dns6TxTokens, Token, &TokenEntry);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  } else {
    TokenEntry = NULL;
  }

  //
  // Cancel this TokenEntry from the Dns6TxTokens map.
  //
  Status = NetMapIterate (&Instance->Dns6TxTokens, Dns6CancelTokens, TokenEntry);

  if ((TokenEntry != NULL) && (Status == EFI_ABORTED)) {
    //
    // If Token isn't NULL and Status is EFI_ABORTED, the token is cancelled from
    // the Dns6TxTokens and returns success.
    //
    if (NetMapIsEmpty (&Instance->Dns6TxTokens)) {
       Instance->UdpIo->Protocol.Udp6->Cancel (Instance->UdpIo->Protocol.Udp6, &Instance->UdpIo->RecvRequest->Token.Udp6);
    }
    return EFI_SUCCESS;
  }

  ASSERT ((TokenEntry != NULL) || (0 == NetMapGetCount (&Instance->Dns6TxTokens)));
  
  if (NetMapIsEmpty (&Instance->Dns6TxTokens)) {
    Instance->UdpIo->Protocol.Udp6->Cancel (Instance->UdpIo->Protocol.Udp6, &Instance->UdpIo->RecvRequest->Token.Udp6);
  }

  return EFI_SUCCESS;
}

/**
  Free the resource related to the configure parameters.

  @param  Config                 The DNS configure data

**/
VOID
Dns4CleanConfigure (
  IN OUT EFI_DNS4_CONFIG_DATA  *Config
  )
{
  if (Config->DnsServerList != NULL) {
    FreePool (Config->DnsServerList);
  }

  ZeroMem (Config, sizeof (EFI_DNS4_CONFIG_DATA));
}

/**
  Free the resource related to the configure parameters.

  @param  Config                 The DNS configure data

**/
VOID
Dns6CleanConfigure (
  IN OUT EFI_DNS6_CONFIG_DATA  *Config
  )
{
  if (Config->DnsServerList != NULL) {
    FreePool (Config->DnsServerList);
  }

  ZeroMem (Config, sizeof (EFI_DNS6_CONFIG_DATA));
}

/**
  Allocate memory for configure parameter such as timeout value for Dst,
  then copy the configure parameter from Src to Dst.

  @param[out]  Dst               The destination DHCP configure data.
  @param[in]   Src               The source DHCP configure data.

  @retval EFI_OUT_OF_RESOURCES   Failed to allocate memory.
  @retval EFI_SUCCESS            The configure is copied.

**/
EFI_STATUS
Dns4CopyConfigure (
  OUT EFI_DNS4_CONFIG_DATA  *Dst,
  IN  EFI_DNS4_CONFIG_DATA  *Src
  )
{
  UINTN                     Len;
  UINT32                    Index;

  CopyMem (Dst, Src, sizeof (*Dst));
  Dst->DnsServerList = NULL;

  //
  // Allocate a memory then copy DnsServerList to it
  //
  if (Src->DnsServerList != NULL) {
    Len                = Src->DnsServerListCount * sizeof (EFI_IPv4_ADDRESS);
    Dst->DnsServerList = AllocatePool (Len);
    if (Dst->DnsServerList == NULL) {
      Dns4CleanConfigure (Dst);
      return EFI_OUT_OF_RESOURCES;
    }

    for (Index = 0; Index < Src->DnsServerListCount; Index++) {
      CopyMem (&Dst->DnsServerList[Index], &Src->DnsServerList[Index], sizeof (EFI_IPv4_ADDRESS));
    }
  }

  return EFI_SUCCESS;
}

/**
  Allocate memory for configure parameter such as timeout value for Dst,
  then copy the configure parameter from Src to Dst.

  @param[out]  Dst               The destination DHCP configure data.
  @param[in]   Src               The source DHCP configure data.

  @retval EFI_OUT_OF_RESOURCES   Failed to allocate memory.
  @retval EFI_SUCCESS            The configure is copied.

**/
EFI_STATUS
Dns6CopyConfigure (
  OUT EFI_DNS6_CONFIG_DATA  *Dst,
  IN  EFI_DNS6_CONFIG_DATA  *Src
  )
{
  UINTN                     Len;
  UINT32                    Index;

  CopyMem (Dst, Src, sizeof (*Dst));
  Dst->DnsServerList = NULL;

  //
  // Allocate a memory then copy DnsServerList to it
  //
  if (Src->DnsServerList != NULL) {
    Len                = Src->DnsServerCount * sizeof (EFI_IPv6_ADDRESS);
    Dst->DnsServerList = AllocatePool (Len);
    if (Dst->DnsServerList == NULL) {
      Dns6CleanConfigure (Dst);
      return EFI_OUT_OF_RESOURCES;
    }

    for (Index = 0; Index < Src->DnsServerCount; Index++) {
      CopyMem (&Dst->DnsServerList[Index], &Src->DnsServerList[Index], sizeof (EFI_IPv6_ADDRESS));
    }
  }

  return EFI_SUCCESS;
}

/**
  Callback of Dns packet. Does nothing.

  @param Arg           The context.

**/
VOID
EFIAPI
DnsDummyExtFree (
  IN VOID                   *Arg
  )
{
}

/**
  Poll the UDP to get the IP4 default address, which may be retrieved
  by DHCP. 
  
  The default time out value is 5 seconds. If IP has retrieved the default address, 
  the UDP is reconfigured.

  @param  Instance               The DNS instance
  @param  UdpIo                  The UDP_IO to poll
  @param  UdpCfgData             The UDP configure data to reconfigure the UDP_IO

  @retval TRUE                   The default address is retrieved and UDP is reconfigured.
  @retval FALSE                  Some error occured.

**/
BOOLEAN
Dns4GetMapping (
  IN DNS_INSTANCE           *Instance,
  IN UDP_IO                 *UdpIo,
  IN EFI_UDP4_CONFIG_DATA   *UdpCfgData
  )
{
  DNS_SERVICE               *Service;
  EFI_IP4_MODE_DATA         Ip4Mode;
  EFI_UDP4_PROTOCOL         *Udp;
  EFI_STATUS                Status;

  ASSERT (Instance->Dns4CfgData.UseDefaultSetting);

  Service = Instance->Service;
  Udp     = UdpIo->Protocol.Udp4;

  Status = gBS->SetTimer (
                  Service->TimerToGetMap,
                  TimerRelative,
                  DNS_TIME_TO_GETMAP * TICKS_PER_SECOND
                  );
  if (EFI_ERROR (Status)) {
    return FALSE;
  }

  while (!EFI_ERROR (gBS->CheckEvent (Service->TimerToGetMap))) {
    Udp->Poll (Udp);

    if (!EFI_ERROR (Udp->GetModeData (Udp, NULL, &Ip4Mode, NULL, NULL)) &&
        Ip4Mode.IsConfigured) {

      Udp->Configure (Udp, NULL);
      return (BOOLEAN) (Udp->Configure (Udp, UdpCfgData) == EFI_SUCCESS);
    }
  }

  return FALSE;
}

/**
  Configure the opened Udp6 instance until the corresponding Ip6 instance
  has been configured.

  @param  Instance               The DNS instance
  @param  UdpIo                  The UDP_IO to poll
  @param  UdpCfgData             The UDP configure data to reconfigure the UDP_IO

  @retval TRUE                   Configure the Udp6 instance successfully.
  @retval FALSE                  Some error occured.

**/
BOOLEAN
Dns6GetMapping (
  IN DNS_INSTANCE           *Instance,
  IN UDP_IO                 *UdpIo,
  IN EFI_UDP6_CONFIG_DATA   *UdpCfgData
  )
{
  DNS_SERVICE               *Service;
  EFI_IP6_MODE_DATA         Ip6Mode;
  EFI_UDP6_PROTOCOL         *Udp;
  EFI_STATUS                Status;

  Service = Instance->Service;
  Udp     = UdpIo->Protocol.Udp6;

  Status = gBS->SetTimer (
                  Service->TimerToGetMap,
                  TimerRelative,
                  DNS_TIME_TO_GETMAP * TICKS_PER_SECOND
                  );
  if (EFI_ERROR (Status)) {
    return FALSE;
  }

  while (!EFI_ERROR (gBS->CheckEvent (Service->TimerToGetMap))) {
    Udp->Poll (Udp);

    if (!EFI_ERROR (Udp->GetModeData (Udp, NULL, &Ip6Mode, NULL, NULL))) {
      if (Ip6Mode.AddressList != NULL) {
        FreePool (Ip6Mode.AddressList);
      }

      if (Ip6Mode.GroupTable != NULL) {
        FreePool (Ip6Mode.GroupTable);
      }

      if (Ip6Mode.RouteTable != NULL) {
        FreePool (Ip6Mode.RouteTable);
      }

      if (Ip6Mode.NeighborCache != NULL) {
        FreePool (Ip6Mode.NeighborCache);
      }

      if (Ip6Mode.PrefixTable != NULL) {
        FreePool (Ip6Mode.PrefixTable);
      }

      if (Ip6Mode.IcmpTypeList != NULL) {
        FreePool (Ip6Mode.IcmpTypeList);
      }

      if (Ip6Mode.IsConfigured) {
        Udp->Configure (Udp, NULL);
        return (BOOLEAN) (Udp->Configure (Udp, UdpCfgData) == EFI_SUCCESS);
      }
    }
  }

  return FALSE;
}

/**
  Configure the UDP.
  
  @param  Instance               The DNS session
  @param  UdpIo                  The UDP_IO instance
  
  @retval EFI_SUCCESS            The UDP is successfully configured for the
                                 session.

**/
EFI_STATUS
Dns4ConfigUdp (
  IN DNS_INSTANCE           *Instance,
  IN UDP_IO                 *UdpIo
  )
{
  EFI_DNS4_CONFIG_DATA      *Config;
  EFI_UDP4_CONFIG_DATA      UdpConfig;
  EFI_STATUS                Status;

  Config = &Instance->Dns4CfgData;

  UdpConfig.AcceptBroadcast    = FALSE;
  UdpConfig.AcceptPromiscuous  = FALSE;
  UdpConfig.AcceptAnyPort      = FALSE;
  UdpConfig.AllowDuplicatePort = FALSE;
  UdpConfig.TypeOfService      = 0;
  UdpConfig.TimeToLive         = 128;
  UdpConfig.DoNotFragment      = FALSE;
  UdpConfig.ReceiveTimeout     = 0;
  UdpConfig.TransmitTimeout    = 0;
  UdpConfig.UseDefaultAddress  = Config->UseDefaultSetting;
  UdpConfig.SubnetMask         = Config->SubnetMask;
  UdpConfig.StationPort        = Config->LocalPort;
  UdpConfig.RemotePort         = DNS_SERVER_PORT;

  CopyMem (&UdpConfig.StationAddress, &Config->StationIp, sizeof (EFI_IPv4_ADDRESS));
  CopyMem (&UdpConfig.RemoteAddress, &Instance->SessionDnsServer.v4, sizeof (EFI_IPv4_ADDRESS));

  Status = UdpIo->Protocol.Udp4->Configure (UdpIo->Protocol.Udp4, &UdpConfig);

  if ((Status == EFI_NO_MAPPING) && Dns4GetMapping (Instance, UdpIo, &UdpConfig)) {
    return EFI_SUCCESS;
  }
  
  return Status;
}

/**
  Configure the UDP.
  
  @param  Instance               The DNS session
  @param  UdpIo                  The UDP_IO instance

  @retval EFI_SUCCESS            The UDP is successfully configured for the
                                 session.

**/
EFI_STATUS
Dns6ConfigUdp (
  IN DNS_INSTANCE           *Instance,
  IN UDP_IO                 *UdpIo
  )
{
  EFI_DNS6_CONFIG_DATA      *Config;
  EFI_UDP6_CONFIG_DATA      UdpConfig;
  EFI_STATUS                Status;

  Config = &Instance->Dns6CfgData;

  UdpConfig.AcceptPromiscuous  = FALSE;
  UdpConfig.AcceptAnyPort      = FALSE;
  UdpConfig.AllowDuplicatePort = FALSE;
  UdpConfig.TrafficClass       = 0;
  UdpConfig.HopLimit           = 128;
  UdpConfig.ReceiveTimeout     = 0;
  UdpConfig.TransmitTimeout    = 0;
  UdpConfig.StationPort        = Config->LocalPort;
  UdpConfig.RemotePort         = DNS_SERVER_PORT;
  CopyMem (&UdpConfig.StationAddress, &Config->StationIp, sizeof (EFI_IPv6_ADDRESS));
  CopyMem (&UdpConfig.RemoteAddress, &Instance->SessionDnsServer.v6, sizeof (EFI_IPv6_ADDRESS));

  Status = UdpIo->Protocol.Udp6->Configure (UdpIo->Protocol.Udp6, &UdpConfig);

  if ((Status == EFI_NO_MAPPING) && Dns6GetMapping (Instance, UdpIo, &UdpConfig)) {
    return EFI_SUCCESS;
  }
  
  return Status;
}

/**
  Update Dns4 cache to shared list of caches of all DNSv4 instances.
  
  @param  Dns4CacheList      All Dns4 cache list.
  @param  DeleteFlag         If FALSE, this function is to add one entry to the DNS Cache. 
                             If TRUE, this function will delete matching DNS Cache entry. 
  @param  Override           If TRUE, the matching DNS cache entry will be overwritten with the supplied parameter. 
                             If FALSE, EFI_ACCESS_DENIED will be returned if the entry to be added is already exists.
  @param  DnsCacheEntry      Entry Pointer to DNS Cache entry.

  @retval EFI_SUCCESS        Update Dns4 cache successfully.
  @retval Others             Failed to update Dns4 cache.   
  
**/ 
EFI_STATUS
EFIAPI
UpdateDns4Cache (
  IN LIST_ENTRY             *Dns4CacheList,
  IN BOOLEAN                DeleteFlag,
  IN BOOLEAN                Override,
  IN EFI_DNS4_CACHE_ENTRY   DnsCacheEntry
  )
{
  DNS4_CACHE    *NewDnsCache;  
  DNS4_CACHE    *Item;
  LIST_ENTRY    *Entry;
  LIST_ENTRY    *Next;

  NewDnsCache = NULL;
  Item        = NULL;
  
  //
  // Search the database for the matching EFI_DNS_CACHE_ENTRY
  //
  NET_LIST_FOR_EACH_SAFE (Entry, Next, Dns4CacheList) {
    Item = NET_LIST_USER_STRUCT (Entry, DNS4_CACHE, AllCacheLink);
    if (StrCmp (DnsCacheEntry.HostName, Item->DnsCache.HostName) == 0 && \
        CompareMem (DnsCacheEntry.IpAddress, Item->DnsCache.IpAddress, sizeof (EFI_IPv4_ADDRESS)) == 0) {
      //
      // This is the Dns cache entry
      //
      if (DeleteFlag) {
        //
        // Delete matching DNS Cache entry
        //
        RemoveEntryList (&Item->AllCacheLink);
        
        return EFI_SUCCESS;
      } else if (Override) {
        //
        // Update this one
        //
        Item->DnsCache.Timeout = DnsCacheEntry.Timeout;
        
        return EFI_SUCCESS;
      }else {
        return EFI_ACCESS_DENIED;
      }
    }
  }

  //
  // Add new one
  //
  NewDnsCache = AllocatePool (sizeof (DNS4_CACHE));
  if (NewDnsCache == NULL) { 
    return EFI_OUT_OF_RESOURCES;
  }
  
  InitializeListHead (&NewDnsCache->AllCacheLink);
   
  NewDnsCache->DnsCache.HostName = AllocatePool (StrSize (DnsCacheEntry.HostName));
  if (NewDnsCache->DnsCache.HostName == NULL) { 
    return EFI_OUT_OF_RESOURCES;
  }
  
  CopyMem (NewDnsCache->DnsCache.HostName, DnsCacheEntry.HostName, StrSize (DnsCacheEntry.HostName));

  NewDnsCache->DnsCache.IpAddress = AllocatePool (sizeof (EFI_IPv4_ADDRESS));
  if (NewDnsCache->DnsCache.IpAddress == NULL) { 
    return EFI_OUT_OF_RESOURCES;
  }

  CopyMem (NewDnsCache->DnsCache.IpAddress, DnsCacheEntry.IpAddress, sizeof (EFI_IPv4_ADDRESS));

  NewDnsCache->DnsCache.Timeout = DnsCacheEntry.Timeout;
  
  InsertTailList (Dns4CacheList, &NewDnsCache->AllCacheLink);
  
  return EFI_SUCCESS;
}

/**
  Update Dns6 cache to shared list of caches of all DNSv6 instances. 

  @param  Dns6CacheList      All Dns6 cache list.
  @param  DeleteFlag         If FALSE, this function is to add one entry to the DNS Cache. 
                             If TRUE, this function will delete matching DNS Cache entry. 
  @param  Override           If TRUE, the matching DNS cache entry will be overwritten with the supplied parameter. 
                             If FALSE, EFI_ACCESS_DENIED will be returned if the entry to be added is already exists.
  @param  DnsCacheEntry      Entry Pointer to DNS Cache entry.
  
  @retval EFI_SUCCESS        Update Dns6 cache successfully.
  @retval Others             Failed to update Dns6 cache.
**/ 
EFI_STATUS
EFIAPI
UpdateDns6Cache (
  IN LIST_ENTRY             *Dns6CacheList,
  IN BOOLEAN                DeleteFlag,
  IN BOOLEAN                Override,
  IN EFI_DNS6_CACHE_ENTRY   DnsCacheEntry
  )
{
  DNS6_CACHE    *NewDnsCache;  
  DNS6_CACHE    *Item;
  LIST_ENTRY    *Entry;
  LIST_ENTRY    *Next;

  NewDnsCache = NULL;
  Item        = NULL;
  
  //
  // Search the database for the matching EFI_DNS_CACHE_ENTRY
  //
  NET_LIST_FOR_EACH_SAFE (Entry, Next, Dns6CacheList) {
    Item = NET_LIST_USER_STRUCT (Entry, DNS6_CACHE, AllCacheLink);
    if (StrCmp (DnsCacheEntry.HostName, Item->DnsCache.HostName) == 0 && \
        CompareMem (DnsCacheEntry.IpAddress, Item->DnsCache.IpAddress, sizeof (EFI_IPv6_ADDRESS)) == 0) {
      //
      // This is the Dns cache entry
      //
      if (DeleteFlag) {
        //
        // Delete matching DNS Cache entry
        //
        RemoveEntryList (&Item->AllCacheLink);
        
        return EFI_SUCCESS;
      } else if (Override) {
        //
        // Update this one
        //
        Item->DnsCache.Timeout = DnsCacheEntry.Timeout;
        
        return EFI_SUCCESS;
      }else {
        return EFI_ACCESS_DENIED;
      }
    }
  }

  //
  // Add new one
  //
  NewDnsCache = AllocatePool (sizeof (DNS6_CACHE));
  if (NewDnsCache == NULL) { 
    return EFI_OUT_OF_RESOURCES;
  }
  
  InitializeListHead (&NewDnsCache->AllCacheLink);
   
  NewDnsCache->DnsCache.HostName = AllocatePool (StrSize (DnsCacheEntry.HostName));
  if (NewDnsCache->DnsCache.HostName == NULL) { 
    return EFI_OUT_OF_RESOURCES;
  }
  
  CopyMem (NewDnsCache->DnsCache.HostName, DnsCacheEntry.HostName, StrSize (DnsCacheEntry.HostName));

  NewDnsCache->DnsCache.IpAddress = AllocatePool (sizeof (EFI_IPv6_ADDRESS));
  if (NewDnsCache->DnsCache.IpAddress == NULL) { 
    return EFI_OUT_OF_RESOURCES;
  }
  
  CopyMem (NewDnsCache->DnsCache.IpAddress, DnsCacheEntry.IpAddress, sizeof (EFI_IPv6_ADDRESS));

  NewDnsCache->DnsCache.Timeout = DnsCacheEntry.Timeout;
  
  InsertTailList (Dns6CacheList, &NewDnsCache->AllCacheLink);
  
  return EFI_SUCCESS;
}

/**
  Add Dns4 ServerIp to common list of addresses of all configured DNSv4 server. 

  @param  Dns4ServerList    Common list of addresses of all configured DNSv4 server.  
  @param  ServerIp          DNS server Ip.  

  @retval EFI_SUCCESS       Add Dns4 ServerIp to common list successfully.
  @retval Others            Failed to add Dns4 ServerIp to common list.
  
**/ 
EFI_STATUS
EFIAPI
AddDns4ServerIp (
  IN LIST_ENTRY                *Dns4ServerList,
  IN EFI_IPv4_ADDRESS           ServerIp
  )
{
  DNS4_SERVER_IP    *NewServerIp;  
  DNS4_SERVER_IP    *Item;
  LIST_ENTRY        *Entry;
  LIST_ENTRY        *Next;

  NewServerIp = NULL;
  Item        = NULL;
  
  //
  // Search the database for the matching ServerIp
  //
  NET_LIST_FOR_EACH_SAFE (Entry, Next, Dns4ServerList) {
    Item = NET_LIST_USER_STRUCT (Entry, DNS4_SERVER_IP, AllServerLink);
    if (CompareMem (&Item->Dns4ServerIp, &ServerIp, sizeof (EFI_IPv4_ADDRESS)) == 0) {
      //
      // Already done.
      // 
      return EFI_SUCCESS;
    }
  }

  //
  // Add new one
  //
  NewServerIp = AllocatePool (sizeof (DNS4_SERVER_IP));
  if (NewServerIp == NULL) { 
    return EFI_OUT_OF_RESOURCES;
  }
  
  InitializeListHead (&NewServerIp->AllServerLink);
   
  CopyMem (&NewServerIp->Dns4ServerIp, &ServerIp, sizeof (EFI_IPv4_ADDRESS));
  
  InsertTailList (Dns4ServerList, &NewServerIp->AllServerLink);
  
  return EFI_SUCCESS;
}

/**
  Add Dns6 ServerIp to common list of addresses of all configured DNSv6 server. 

  @param  Dns6ServerList    Common list of addresses of all configured DNSv6 server.  
  @param  ServerIp          DNS server Ip.  

  @retval EFI_SUCCESS       Add Dns6 ServerIp to common list successfully.
  @retval Others            Failed to add Dns6 ServerIp to common list.
  
**/ 
EFI_STATUS
EFIAPI
AddDns6ServerIp (
  IN LIST_ENTRY                *Dns6ServerList,
  IN EFI_IPv6_ADDRESS           ServerIp
  )
{
  DNS6_SERVER_IP    *NewServerIp;  
  DNS6_SERVER_IP    *Item;
  LIST_ENTRY        *Entry;
  LIST_ENTRY        *Next;

  NewServerIp = NULL;
  Item        = NULL;
  
  //
  // Search the database for the matching ServerIp
  //
  NET_LIST_FOR_EACH_SAFE (Entry, Next, Dns6ServerList) {
    Item = NET_LIST_USER_STRUCT (Entry, DNS6_SERVER_IP, AllServerLink);
    if (CompareMem (&Item->Dns6ServerIp, &ServerIp, sizeof (EFI_IPv6_ADDRESS)) == 0) {
      //
      // Already done.
      // 
      return EFI_SUCCESS;
    }
  }

  //
  // Add new one
  //
  NewServerIp = AllocatePool (sizeof (DNS6_SERVER_IP));
  if (NewServerIp == NULL) { 
    return EFI_OUT_OF_RESOURCES;
  }
  
  InitializeListHead (&NewServerIp->AllServerLink);
   
  CopyMem (&NewServerIp->Dns6ServerIp, &ServerIp, sizeof (EFI_IPv6_ADDRESS));
  
  InsertTailList (Dns6ServerList, &NewServerIp->AllServerLink);
  
  return EFI_SUCCESS;
}

/**
  Fill QName for IP querying. QName is a domain name represented as 
  a sequence of labels, where each label consists of a length octet 
  followed by that number of octets. The domain name terminates with 
  the zero length octet for the null label of the root. Caller should 
  take responsibility to the buffer in QName.

  @param  HostName          Queried HostName    

  @retval NULL      Failed to fill QName.
  @return           QName filled successfully.
  
**/ 
CHAR8 *
EFIAPI
DnsFillinQNameForQueryIp (
  IN  CHAR16              *HostName
  )
{
  CHAR8                 *QueryName;
  CHAR8                 *Header;
  CHAR8                 *Tail;
  UINTN                 Len;
  UINTN                 Index;

  QueryName  = NULL;
  Header     = NULL;
  Tail       = NULL;

  QueryName = AllocateZeroPool (DNS_DEFAULT_BLKSIZE);
  if (QueryName == NULL) {
    return NULL;
  }
  
  Header = QueryName;
  Tail = Header + 1;
  Len = 0;
  for (Index = 0; HostName[Index] != 0; Index++) {
    *Tail = (CHAR8) HostName[Index];
    if (*Tail == '.') {
      *Header = (CHAR8) Len;
      Header = Tail;
      Tail ++;
      Len = 0;
    } else {
      Tail++;
      Len++;
    }
  }
  *Header = (CHAR8) Len;
  *Tail = 0;

  return QueryName;
}

/**
  Find out whether the response is valid or invalid.

  @param  TokensMap       All DNS transmittal Tokens entry.  
  @param  Identification  Identification for queried packet.  
  @param  Type            Type for queried packet.
  @param  Item            Return corresponding Token entry.

  @retval TRUE            The response is valid.
  @retval FALSE           The response is invalid.
  
**/ 
BOOLEAN
IsValidDnsResponse (
  IN     NET_MAP      *TokensMap,
  IN     UINT16       Identification,
  IN     UINT16       Type,
     OUT NET_MAP_ITEM **Item
  )
{
  LIST_ENTRY              *Entry;

  NET_BUF                 *Packet;
  UINT8                   *TxString;
  DNS_HEADER              *DnsHeader;
  CHAR8                   *QueryName;
  DNS_QUERY_SECTION       *QuerySection;

  NET_LIST_FOR_EACH (Entry, &TokensMap->Used) {
    *Item = NET_LIST_USER_STRUCT (Entry, NET_MAP_ITEM, Link);
    Packet = (NET_BUF *) ((*Item)->Value);
    if (Packet == NULL){
      
      continue;
    } else {
      TxString = NetbufGetByte (Packet, 0, NULL);
      ASSERT (TxString != NULL);
      DnsHeader = (DNS_HEADER *) TxString;
      QueryName = (CHAR8 *) (TxString + sizeof (*DnsHeader));
      QuerySection = (DNS_QUERY_SECTION *) (QueryName + AsciiStrLen (QueryName) + 1);

      DnsHeader->Identification = NTOHS (DnsHeader->Identification);
      QuerySection->Type = NTOHS (QuerySection->Type);
        
      if (DnsHeader->Identification == Identification && QuerySection->Type == Type) {
        return TRUE;
      }
    } 
  }
  
  *Item =NULL;
  
  return FALSE;
}

/**
  Parse Dns Response.

  @param  Instance              The DNS instance
  @param  RxString              Received buffer.
  @param  Completed             Flag to indicate that Dns response is valid. 
  
  @retval EFI_SUCCESS           Parse Dns Response successfully.
  @retval Others                Failed to parse Dns Response.
  
**/ 
EFI_STATUS
ParseDnsResponse (
  IN OUT DNS_INSTANCE              *Instance,
  IN     UINT8                     *RxString,
     OUT BOOLEAN                   *Completed
  )
{
  DNS_HEADER            *DnsHeader;
  
  CHAR8                 *QueryName;
  DNS_QUERY_SECTION     *QuerySection;
  
  CHAR8                 *AnswerName;
  DNS_ANSWER_SECTION    *AnswerSection;
  UINT8                 *AnswerData;

  NET_MAP_ITEM          *Item;
  DNS4_TOKEN_ENTRY      *Dns4TokenEntry;
  DNS6_TOKEN_ENTRY      *Dns6TokenEntry;
  
  UINT32                IpCount;
  UINT32                RRCount;
  UINT32                AnswerSectionNum;
  
  EFI_IPv4_ADDRESS      *HostAddr4;
  EFI_IPv6_ADDRESS      *HostAddr6;

  EFI_DNS4_CACHE_ENTRY  *Dns4CacheEntry;
  EFI_DNS6_CACHE_ENTRY  *Dns6CacheEntry;

  DNS_RESOURCE_RECORD   *Dns4RR;
  DNS6_RESOURCE_RECORD  *Dns6RR;

  EFI_STATUS            Status;

  EFI_TPL               OldTpl;
  
  Item             = NULL;
  Dns4TokenEntry   = NULL;
  Dns6TokenEntry   = NULL;
  
  IpCount          = 0;
  RRCount          = 0;
  AnswerSectionNum = 0;
  
  HostAddr4        = NULL;
  HostAddr6        = NULL;
  
  Dns4CacheEntry   = NULL;
  Dns6CacheEntry   = NULL;
  
  Dns4RR           = NULL;
  Dns6RR           = NULL;

  *Completed       = TRUE;
  Status           = EFI_SUCCESS;
  
  //
  // Get header
  //
  DnsHeader = (DNS_HEADER *) RxString;
  
  DnsHeader->Identification = NTOHS (DnsHeader->Identification);
  DnsHeader->Flags.Uint16 = NTOHS (DnsHeader->Flags.Uint16);
  DnsHeader->QuestionsNum = NTOHS (DnsHeader->QuestionsNum);
  DnsHeader->AnswersNum = NTOHS (DnsHeader->AnswersNum);
  DnsHeader->AuthorityNum = NTOHS (DnsHeader->AuthorityNum);
  DnsHeader->AditionalNum = NTOHS (DnsHeader->AditionalNum);

  //
  // Get Query name
  //
  QueryName = (CHAR8 *) (RxString + sizeof (*DnsHeader));

  //
  // Get query section
  //
  QuerySection = (DNS_QUERY_SECTION *) (QueryName + AsciiStrLen (QueryName) + 1);
  QuerySection->Type = NTOHS (QuerySection->Type);
  QuerySection->Class = NTOHS (QuerySection->Class);

  //
  // Get Answer name
  //
  AnswerName = (CHAR8 *) QuerySection + sizeof (*QuerySection);

  OldTpl = gBS->RaiseTPL (TPL_CALLBACK);

  //
  // Check DnsResponse Validity, if so, also get a valid NET_MAP_ITEM.
  //
  if (Instance->Service->IpVersion == IP_VERSION_4) {
    if (!IsValidDnsResponse (&Instance->Dns4TxTokens, DnsHeader->Identification, QuerySection->Type, &Item)) {
      *Completed = FALSE;
      Status = EFI_ABORTED;
      goto ON_EXIT;
    }
    ASSERT (Item != NULL);
    Dns4TokenEntry = (DNS4_TOKEN_ENTRY *) (Item->Key);
  } else {
    if (!IsValidDnsResponse (&Instance->Dns6TxTokens, DnsHeader->Identification, QuerySection->Type, &Item)) {
      *Completed = FALSE;
      Status = EFI_ABORTED;
      goto ON_EXIT;
    }
    ASSERT (Item != NULL);
    Dns6TokenEntry = (DNS6_TOKEN_ENTRY *) (Item->Key);
  }
   
  //
  // Continue Check Some Errors.
  //
  if (DnsHeader->Flags.Bits.RCode != DNS_FLAGS_RCODE_NO_ERROR || DnsHeader->AnswersNum < 1 || \
      DnsHeader->Flags.Bits.QR != DNS_FLAGS_QR_RESPONSE || QuerySection->Class != DNS_CLASS_INET) {
      Status = EFI_ABORTED;
      goto ON_EXIT;
  }

  //
  // Free the sending packet.
  //
  if (Item->Value != NULL) {
    NetbufFree ((NET_BUF *) (Item->Value));
  }
  
  //
  // Do some buffer allocations.
  //
  if (Instance->Service->IpVersion == IP_VERSION_4) {
    ASSERT (Dns4TokenEntry != NULL);

    if (Dns4TokenEntry->GeneralLookUp) {
      //
      // It's the GeneralLookUp querying.
      //
      Dns4TokenEntry->Token->RspData.GLookupData = AllocatePool (sizeof (DNS_RESOURCE_RECORD));
      if (Dns4TokenEntry->Token->RspData.GLookupData == NULL) {
        Status = EFI_OUT_OF_RESOURCES;
        goto ON_EXIT;
      }
      Dns4TokenEntry->Token->RspData.GLookupData->RRList = AllocatePool (DnsHeader->AnswersNum * sizeof (DNS_RESOURCE_RECORD));
      if (Dns4TokenEntry->Token->RspData.GLookupData->RRList == NULL) {
        Status = EFI_OUT_OF_RESOURCES;
        goto ON_EXIT;
      }
    } else {
      //
      // It's not the GeneralLookUp querying. Check the Query type.
      //
      if (QuerySection->Type == DNS_TYPE_A) {
        Dns4TokenEntry->Token->RspData.H2AData = AllocatePool (sizeof (DNS_HOST_TO_ADDR_DATA));
        if (Dns4TokenEntry->Token->RspData.H2AData == NULL) {
          Status = EFI_OUT_OF_RESOURCES;
          goto ON_EXIT;
        }
        Dns4TokenEntry->Token->RspData.H2AData->IpList = AllocatePool (DnsHeader->AnswersNum * sizeof (EFI_IPv4_ADDRESS));
        if (Dns4TokenEntry->Token->RspData.H2AData->IpList == NULL) {
          Status = EFI_OUT_OF_RESOURCES;
          goto ON_EXIT;
        }
      } else {
        Status = EFI_UNSUPPORTED;
        goto ON_EXIT;
      }
    }
  } else {
    ASSERT (Dns6TokenEntry != NULL);

    if (Dns6TokenEntry->GeneralLookUp) {
      //
      // It's the GeneralLookUp querying.
      //
      Dns6TokenEntry->Token->RspData.GLookupData = AllocatePool (sizeof (DNS_RESOURCE_RECORD));
      if (Dns6TokenEntry->Token->RspData.GLookupData == NULL) {
        Status = EFI_UNSUPPORTED;
        goto ON_EXIT;
      }
      Dns6TokenEntry->Token->RspData.GLookupData->RRList = AllocatePool (DnsHeader->AnswersNum * sizeof (DNS_RESOURCE_RECORD));
      if (Dns6TokenEntry->Token->RspData.GLookupData->RRList == NULL) {
        Status = EFI_UNSUPPORTED;
        goto ON_EXIT;
      }
    } else {
      //
      // It's not the GeneralLookUp querying. Check the Query type.
      //
      if (QuerySection->Type == DNS_TYPE_AAAA) {
        Dns6TokenEntry->Token->RspData.H2AData = AllocatePool (sizeof (DNS6_HOST_TO_ADDR_DATA));
        if (Dns6TokenEntry->Token->RspData.H2AData == NULL) {
          Status = EFI_UNSUPPORTED;
          goto ON_EXIT;
        }
        Dns6TokenEntry->Token->RspData.H2AData->IpList = AllocatePool (DnsHeader->AnswersNum * sizeof (EFI_IPv6_ADDRESS));
        if (Dns6TokenEntry->Token->RspData.H2AData->IpList == NULL) {
          Status = EFI_UNSUPPORTED;
          goto ON_EXIT;
        }
      } else {
        Status = EFI_UNSUPPORTED;
        goto ON_EXIT;
      }
    }
  }

  //
  // Processing AnswerSection.
  //
  while (AnswerSectionNum < DnsHeader->AnswersNum) {
    //
    // Answer name should be PTR.
    //
    ASSERT ((*(UINT8 *) AnswerName & 0xC0) == 0xC0);
    
    //
    // Get Answer section.
    //
    AnswerSection = (DNS_ANSWER_SECTION *) (AnswerName + sizeof (UINT16));
    AnswerSection->Type = NTOHS (AnswerSection->Type);
    AnswerSection->Class = NTOHS (AnswerSection->Class);
    AnswerSection->Ttl = NTOHL (AnswerSection->Ttl);
    AnswerSection->DataLength = NTOHS (AnswerSection->DataLength);

    //
    // Check whether it's the GeneralLookUp querying.
    //
    if (Instance->Service->IpVersion == IP_VERSION_4 && Dns4TokenEntry->GeneralLookUp) {
      Dns4RR = Dns4TokenEntry->Token->RspData.GLookupData->RRList;
      AnswerData = (UINT8 *) AnswerSection + sizeof (*AnswerSection);

      //
      // Fill the ResourceRecord.
      //
      Dns4RR[RRCount].QName = AllocateZeroPool (AsciiStrLen (QueryName) + 1);
      if (Dns4RR[RRCount].QName == NULL) {
        Status = EFI_UNSUPPORTED;
        goto ON_EXIT;
      }
      CopyMem (Dns4RR[RRCount].QName, QueryName, AsciiStrLen (QueryName));
      Dns4RR[RRCount].QType = AnswerSection->Type;
      Dns4RR[RRCount].QClass = AnswerSection->Class;
      Dns4RR[RRCount].TTL = AnswerSection->Ttl;
      Dns4RR[RRCount].DataLength = AnswerSection->DataLength;
      Dns4RR[RRCount].RData = AllocateZeroPool (Dns4RR[RRCount].DataLength);
      if (Dns4RR[RRCount].RData == NULL) {
        Status = EFI_UNSUPPORTED;
        goto ON_EXIT;
      }
      CopyMem (Dns4RR[RRCount].RData, AnswerData, Dns4RR[RRCount].DataLength);
      
      RRCount ++;
    } else if (Instance->Service->IpVersion == IP_VERSION_6 && Dns6TokenEntry->GeneralLookUp) {
      Dns6RR = Dns6TokenEntry->Token->RspData.GLookupData->RRList;
      AnswerData = (UINT8 *) AnswerSection + sizeof (*AnswerSection);

      //
      // Fill the ResourceRecord.
      //
      Dns6RR[RRCount].QName = AllocateZeroPool (AsciiStrLen (QueryName) + 1);
      if (Dns6RR[RRCount].QName == NULL) {
        Status = EFI_UNSUPPORTED;
        goto ON_EXIT;
      }
      CopyMem (Dns6RR[RRCount].QName, QueryName, AsciiStrLen (QueryName));
      Dns6RR[RRCount].QType = AnswerSection->Type;
      Dns6RR[RRCount].QClass = AnswerSection->Class;
      Dns6RR[RRCount].TTL = AnswerSection->Ttl;
      Dns6RR[RRCount].DataLength = AnswerSection->DataLength;
      Dns6RR[RRCount].RData = AllocateZeroPool (Dns6RR[RRCount].DataLength);
      if (Dns6RR[RRCount].RData == NULL) {
        Status = EFI_UNSUPPORTED;
        goto ON_EXIT;
      }
      CopyMem (Dns6RR[RRCount].RData, AnswerData, Dns6RR[RRCount].DataLength);
      
      RRCount ++;
    } else {
      //
      // It's not the GeneralLookUp querying. 
      // Check the Query type, parse the response packet.
      //
      switch (AnswerSection->Type) {
      case DNS_TYPE_A:
        //
        // This is address entry, get Data.
        //
        ASSERT (Dns4TokenEntry != NULL && AnswerSection->DataLength == 4);
        
        HostAddr4 = Dns4TokenEntry->Token->RspData.H2AData->IpList;
        AnswerData = (UINT8 *) AnswerSection + sizeof (*AnswerSection);
        CopyMem (&HostAddr4[IpCount], AnswerData, sizeof (EFI_IPv4_ADDRESS));

        //
        // Update DNS cache dynamically.
        //
        if (Dns4CacheEntry != NULL) {
          if (Dns4CacheEntry->HostName != NULL) {
            FreePool (Dns4CacheEntry->HostName);
          }

          if (Dns4CacheEntry->IpAddress != NULL) {
            FreePool (Dns4CacheEntry->IpAddress);
          }
          
          FreePool (Dns4CacheEntry);
        }

        // 
        // Allocate new CacheEntry pool.
        //
        Dns4CacheEntry = AllocateZeroPool (sizeof (EFI_DNS4_CACHE_ENTRY));
        if (Dns4CacheEntry == NULL) {
          Status = EFI_UNSUPPORTED;
          goto ON_EXIT;
        }
        Dns4CacheEntry->HostName = AllocateZeroPool (2 * (StrLen(Dns4TokenEntry->QueryHostName) + 1));
        if (Dns4CacheEntry->HostName == NULL) {
          Status = EFI_UNSUPPORTED;
          goto ON_EXIT;
        }
        CopyMem (Dns4CacheEntry->HostName, Dns4TokenEntry->QueryHostName, 2 * (StrLen(Dns4TokenEntry->QueryHostName) + 1));
        Dns4CacheEntry->IpAddress = AllocateZeroPool (sizeof (EFI_IPv4_ADDRESS));
        if (Dns4CacheEntry->IpAddress == NULL) {
          Status = EFI_UNSUPPORTED;
          goto ON_EXIT;
        }
        CopyMem (Dns4CacheEntry->IpAddress, AnswerData, sizeof (EFI_IPv4_ADDRESS));
        Dns4CacheEntry->Timeout = AnswerSection->Ttl;
        
        UpdateDns4Cache (&mDriverData->Dns4CacheList, FALSE, TRUE, *Dns4CacheEntry);  

        IpCount ++; 
        break;
      case DNS_TYPE_AAAA:
        //
        // This is address entry, get Data.
        //
        ASSERT (Dns6TokenEntry != NULL && AnswerSection->DataLength == 16);
        
        HostAddr6 = Dns6TokenEntry->Token->RspData.H2AData->IpList;
        AnswerData = (UINT8 *) AnswerSection + sizeof (*AnswerSection);
        CopyMem (&HostAddr6[IpCount], AnswerData, sizeof (EFI_IPv6_ADDRESS));

        //
        // Update DNS cache dynamically.
        //
        if (Dns6CacheEntry != NULL) {
          if (Dns6CacheEntry->HostName != NULL) {
            FreePool (Dns6CacheEntry->HostName);
          }

          if (Dns6CacheEntry->IpAddress != NULL) {
            FreePool (Dns6CacheEntry->IpAddress);
          }
          
          FreePool (Dns6CacheEntry);
        }

        // 
        // Allocate new CacheEntry pool.
        //
        Dns6CacheEntry = AllocateZeroPool (sizeof (EFI_DNS6_CACHE_ENTRY));
        if (Dns6CacheEntry == NULL) {
          Status = EFI_UNSUPPORTED;
          goto ON_EXIT;
        }
        Dns6CacheEntry->HostName = AllocateZeroPool (2 * (StrLen(Dns6TokenEntry->QueryHostName) + 1));
        if (Dns6CacheEntry->HostName == NULL) {
          Status = EFI_UNSUPPORTED;
          goto ON_EXIT;
        }
        CopyMem (Dns6CacheEntry->HostName, Dns6TokenEntry->QueryHostName, 2 * (StrLen(Dns6TokenEntry->QueryHostName) + 1));
        Dns6CacheEntry->IpAddress = AllocateZeroPool (sizeof (EFI_IPv6_ADDRESS));
        if (Dns6CacheEntry->IpAddress == NULL) {
          Status = EFI_UNSUPPORTED;
          goto ON_EXIT;
        }
        CopyMem (Dns6CacheEntry->IpAddress, AnswerData, sizeof (EFI_IPv6_ADDRESS));
        Dns6CacheEntry->Timeout = AnswerSection->Ttl;
        
        UpdateDns6Cache (&mDriverData->Dns6CacheList, FALSE, TRUE, *Dns6CacheEntry);  
        
        IpCount ++;
        break;
      default:
        Status = EFI_UNSUPPORTED;
        goto ON_EXIT;
      }
    }
    
    //
    // Find next one
    //
    AnswerName = (CHAR8 *) AnswerSection + sizeof (*AnswerSection) + AnswerSection->DataLength;
    AnswerSectionNum ++;
  }

  if (Instance->Service->IpVersion == IP_VERSION_4) {
    ASSERT (Dns4TokenEntry != NULL);
    
    if (Dns4TokenEntry->GeneralLookUp) {
      Dns4TokenEntry->Token->RspData.GLookupData->RRCount = RRCount;
    } else {
      if (QuerySection->Type == DNS_TYPE_A) {
        Dns4TokenEntry->Token->RspData.H2AData->IpCount = IpCount;
      } else {
        Status = EFI_UNSUPPORTED;
        goto ON_EXIT;
      }
    }
  } else {
    ASSERT (Dns6TokenEntry != NULL);

    if (Dns6TokenEntry->GeneralLookUp) {
      Dns6TokenEntry->Token->RspData.GLookupData->RRCount = RRCount;
    } else {
      if (QuerySection->Type == DNS_TYPE_AAAA) {
        Dns6TokenEntry->Token->RspData.H2AData->IpCount = IpCount;
      } else {
        Status = EFI_UNSUPPORTED;
        goto ON_EXIT;
      }
    }
  }

  //
  // Parsing is complete, SignalEvent here.
  //
  if (Instance->Service->IpVersion == IP_VERSION_4) {
    ASSERT (Dns4TokenEntry != NULL);
    Dns4RemoveTokenEntry (&Instance->Dns4TxTokens, Dns4TokenEntry);
    Dns4TokenEntry->Token->Status = EFI_SUCCESS;
    if (Dns4TokenEntry->Token->Event != NULL) {
      gBS->SignalEvent (Dns4TokenEntry->Token->Event);
      DispatchDpc ();
    }
  } else {
    ASSERT (Dns6TokenEntry != NULL);
    Dns6RemoveTokenEntry (&Instance->Dns6TxTokens, Dns6TokenEntry);
    Dns6TokenEntry->Token->Status = EFI_SUCCESS;
    if (Dns6TokenEntry->Token->Event != NULL) {
      gBS->SignalEvent (Dns6TokenEntry->Token->Event);
      DispatchDpc ();
    }
  }

  // 
  // Free allocated CacheEntry pool.
  //
  if (Dns4CacheEntry != NULL) {
    if (Dns4CacheEntry->HostName != NULL) {
      FreePool (Dns4CacheEntry->HostName);
    }

    if (Dns4CacheEntry->IpAddress != NULL) {
      FreePool (Dns4CacheEntry->IpAddress);
    }

    FreePool (Dns4CacheEntry);
  }
  
  if (Dns6CacheEntry != NULL) {
    if (Dns6CacheEntry->HostName != NULL) {
      FreePool (Dns6CacheEntry->HostName);
    }

    if (Dns6CacheEntry->IpAddress != NULL) {
      FreePool (Dns6CacheEntry->IpAddress);
    }
  
    FreePool (Dns6CacheEntry);
  }

ON_EXIT:
  gBS->RestoreTPL (OldTpl);
  return Status;
}

/**
  Parse response packet.

  @param  Packet                The packets received.
  @param  EndPoint              The local/remote UDP access point
  @param  IoStatus              The status of the UDP receive
  @param  Context               The opaque parameter to the function.

**/  
VOID
EFIAPI
DnsOnPacketReceived (
  NET_BUF                   *Packet,
  UDP_END_POINT             *EndPoint,
  EFI_STATUS                IoStatus,
  VOID                      *Context
  )
{
  DNS_INSTANCE              *Instance;

  UINT8                     *RcvString;

  BOOLEAN                   Completed;
  
  Instance  = (DNS_INSTANCE *) Context;
  NET_CHECK_SIGNATURE (Instance, DNS_INSTANCE_SIGNATURE);

  RcvString = NULL;
  Completed = FALSE;

  if (EFI_ERROR (IoStatus)) {
    goto ON_EXIT;
  }

  ASSERT (Packet != NULL);
  
  RcvString = NetbufGetByte (Packet, 0, NULL);
  ASSERT (RcvString != NULL);
  
  //
  // Parse Dns Response
  //
  ParseDnsResponse (Instance, RcvString, &Completed);

  ON_EXIT:

    if (Packet != NULL) {
      NetbufFree (Packet);
    }

    if (!Completed) {
      UdpIoRecvDatagram (Instance->UdpIo, DnsOnPacketReceived, Instance, 0);
    }
}

/**
  Release the net buffer when packet is sent.

  @param  Packet                The packets received.
  @param  EndPoint              The local/remote UDP access point
  @param  IoStatus              The status of the UDP receive
  @param  Context               The opaque parameter to the function.

**/
VOID
EFIAPI
DnsOnPacketSent (
  NET_BUF                   *Packet,
  UDP_END_POINT             *EndPoint,
  EFI_STATUS                IoStatus,
  VOID                      *Context
  )
{
  DNS_INSTANCE              *Instance;
  LIST_ENTRY                *Entry;
  NET_MAP_ITEM              *Item;
  DNS4_TOKEN_ENTRY          *Dns4TokenEntry;
  DNS6_TOKEN_ENTRY          *Dns6TokenEntry;

  Dns4TokenEntry = NULL;
  Dns6TokenEntry = NULL;

  Instance  = (DNS_INSTANCE *) Context;
  NET_CHECK_SIGNATURE (Instance, DNS_INSTANCE_SIGNATURE);

  if (Instance->Service->IpVersion == IP_VERSION_4) {
    NET_LIST_FOR_EACH (Entry, &Instance->Dns4TxTokens.Used) {
      Item = NET_LIST_USER_STRUCT (Entry, NET_MAP_ITEM, Link);
      if (Packet == (NET_BUF *)(Item->Value)) {
        Dns4TokenEntry = ((DNS4_TOKEN_ENTRY *)Item->Key);
        Dns4TokenEntry->PacketToLive = Dns4TokenEntry->Token->RetryInterval;
        break;
      }
    }
  } else {
    NET_LIST_FOR_EACH (Entry, &Instance->Dns6TxTokens.Used) {
      Item = NET_LIST_USER_STRUCT (Entry, NET_MAP_ITEM, Link);
      if (Packet == (NET_BUF *)(Item->Value)) {
        Dns6TokenEntry = ((DNS6_TOKEN_ENTRY *)Item->Key);
        Dns6TokenEntry->PacketToLive = Dns6TokenEntry->Token->RetryInterval;
        break;
      }
    }
  }
  
  NetbufFree (Packet);
}

/**
  Query request information.

  @param  Instance              The DNS instance
  @param  Packet                The packet for querying request information.

  @retval EFI_SUCCESS           Query request information successfully.
  @retval Others                Failed to query request information.

**/
EFI_STATUS
DoDnsQuery (
  IN  DNS_INSTANCE              *Instance,
  IN  NET_BUF                   *Packet
  )
{
  EFI_STATUS      Status;

  //
  // Ready to receive the DNS response.
  //
  if (Instance->UdpIo->RecvRequest == NULL) {
    Status = UdpIoRecvDatagram (Instance->UdpIo, DnsOnPacketReceived, Instance, 0);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }
  
  //
  // Transmit the DNS packet.
  //
  NET_GET_REF (Packet);

  Status = UdpIoSendDatagram (Instance->UdpIo, Packet, NULL, NULL, DnsOnPacketSent, Instance);
  
  return Status;
}

/**
  Construct the Packet according query section.

  @param  Instance              The DNS instance
  @param  QueryName             Queried Name  
  @param  Type                  Queried Type 
  @param  Class                 Queried Class 
  @param  Packet                The packet for query

  @retval EFI_SUCCESS           The packet is constructed.
  @retval Others                Failed to construct the Packet.

**/
EFI_STATUS
ConstructDNSQuery (
  IN  DNS_INSTANCE              *Instance,
  IN  CHAR8                     *QueryName,
  IN  UINT16                    Type,
  IN  UINT16                    Class,
  OUT NET_BUF                   **Packet
  )
{
  NET_FRAGMENT        Frag;
  DNS_HEADER          *DnsHeader;
  DNS_QUERY_SECTION   *DnsQuery;
  
  Frag.Bulk = AllocatePool (DNS_DEFAULT_BLKSIZE * sizeof (UINT8));
  if (Frag.Bulk == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  //
  // Fill header
  //
  DnsHeader = (DNS_HEADER *) Frag.Bulk; 
  DnsHeader->Identification = (UINT16)NET_RANDOM (NetRandomInitSeed());
  DnsHeader->Flags.Uint16 = 0x0000;
  DnsHeader->Flags.Bits.RD = 1;
  DnsHeader->Flags.Bits.OpCode = DNS_FLAGS_OPCODE_STANDARD;
  DnsHeader->Flags.Bits.QR = DNS_FLAGS_QR_QUERY;
  DnsHeader->QuestionsNum = 1;
  DnsHeader->AnswersNum = 0;
  DnsHeader->AuthorityNum = 0;
  DnsHeader->AditionalNum = 0;

  DnsHeader->Identification = HTONS (DnsHeader->Identification);
  DnsHeader->Flags.Uint16 = HTONS (DnsHeader->Flags.Uint16);
  DnsHeader->QuestionsNum = HTONS (DnsHeader->QuestionsNum);
  DnsHeader->AnswersNum = HTONS (DnsHeader->AnswersNum);
  DnsHeader->AuthorityNum = HTONS (DnsHeader->AuthorityNum);
  DnsHeader->AditionalNum = HTONS (DnsHeader->AditionalNum);

  Frag.Len = sizeof (*DnsHeader);

  //
  // Fill Query name
  //
  CopyMem (Frag.Bulk + Frag.Len, QueryName, AsciiStrLen (QueryName));
  Frag.Len = (UINT32) (Frag.Len + AsciiStrLen (QueryName));
  *(Frag.Bulk + Frag.Len) = 0;
  Frag.Len ++;
  
  //
  // Rest query section
  //
  DnsQuery = (DNS_QUERY_SECTION *) (Frag.Bulk + Frag.Len);

  DnsQuery->Type = HTONS (Type);
  DnsQuery->Class = HTONS (Class);

  Frag.Len += sizeof (*DnsQuery);

  //
  // Wrap the Frag in a net buffer.
  //
  *Packet = NetbufFromExt (&Frag, 1, 0, 0, DnsDummyExtFree, NULL);
  if (*Packet == NULL) {
    FreePool (Frag.Bulk);
    return EFI_OUT_OF_RESOURCES;
  }
  
  //
  // Store the UdpIo in ProtoData.
  //
  *((UINTN *) &((*Packet)->ProtoData[0])) = (UINTN) (Instance->UdpIo);

  return EFI_SUCCESS;
}

/**
  Retransmit the packet.

  @param  Instance              The DNS instance
  @param  Packet                Retransmit the packet 

  @retval EFI_SUCCESS           The packet is retransmitted.
  @retval Others                Failed to retransmit.

**/
EFI_STATUS
DnsRetransmit (
  IN DNS_INSTANCE        *Instance,
  IN NET_BUF             *Packet
  )
{
  EFI_STATUS      Status;
  
  UINT8           *Buffer;

  ASSERT (Packet != NULL);

  //
  // Set the requests to the listening port, other packets to the connected port
  //
  Buffer = NetbufGetByte (Packet, 0, NULL);
  ASSERT (Buffer != NULL);

  NET_GET_REF (Packet);

  Status = UdpIoSendDatagram (
             Instance->UdpIo,
             Packet,
             NULL,
             NULL,
             DnsOnPacketSent,
             Instance
             );

  if (EFI_ERROR (Status)) {
    NET_PUT_REF (Packet);
  }

  return Status;
}

/**
  The timer ticking function for the DNS services.

  @param  Event                 The ticking event
  @param  Context               The DNS service instance

**/
VOID
EFIAPI
DnsOnTimerRetransmit (
  IN EFI_EVENT              Event,
  IN VOID                   *Context
  )
{
  DNS_SERVICE                *Service;

  LIST_ENTRY                 *Entry;
  LIST_ENTRY                 *Next;

  DNS_INSTANCE               *Instance;
  LIST_ENTRY                 *EntryNetMap;
  NET_MAP_ITEM               *ItemNetMap;
  DNS4_TOKEN_ENTRY           *Dns4TokenEntry;
  DNS6_TOKEN_ENTRY           *Dns6TokenEntry;

  Dns4TokenEntry = NULL;
  Dns6TokenEntry = NULL;

  Service = (DNS_SERVICE *) Context;


  if (Service->IpVersion == IP_VERSION_4) {
    //
    // Iterate through all the children of the DNS service instance. Time
    // out the packet. If maximum retries reached, clean the Token up.
    //
    NET_LIST_FOR_EACH_SAFE (Entry, Next, &Service->Dns4ChildrenList) {
      Instance = NET_LIST_USER_STRUCT (Entry, DNS_INSTANCE, Link);

      EntryNetMap = Instance->Dns4TxTokens.Used.ForwardLink;
      while (EntryNetMap != &Instance->Dns4TxTokens.Used) {
        ItemNetMap = NET_LIST_USER_STRUCT (EntryNetMap, NET_MAP_ITEM, Link);
        Dns4TokenEntry = (DNS4_TOKEN_ENTRY *)(ItemNetMap->Key);
        if (Dns4TokenEntry->PacketToLive == 0 || (--Dns4TokenEntry->PacketToLive > 0)) {
          EntryNetMap = EntryNetMap->ForwardLink;
          continue;
        }

        //
        // Retransmit the packet if haven't reach the maxmium retry count,
        // otherwise exit the transfer.
        //
        if (++Dns4TokenEntry->Token->RetryCount < Instance->MaxRetry) {
          DnsRetransmit (Instance, (NET_BUF *)ItemNetMap->Value);
          EntryNetMap = EntryNetMap->ForwardLink;
        } else {
          //
          // Maximum retries reached, clean the Token up.
          //
          Dns4RemoveTokenEntry (&Instance->Dns4TxTokens, Dns4TokenEntry);
          Dns4TokenEntry->Token->Status = EFI_TIMEOUT;
          gBS->SignalEvent (Dns4TokenEntry->Token->Event);
          DispatchDpc ();
          
          //
          // Free the sending packet.
          //
          if (ItemNetMap->Value != NULL) {
            NetbufFree ((NET_BUF *)(ItemNetMap->Value));
          }

          EntryNetMap = Instance->Dns4TxTokens.Used.ForwardLink;
        }
      }
    } 
  }else {
    //
    // Iterate through all the children of the DNS service instance. Time
    // out the packet. If maximum retries reached, clean the Token up.
    //
    NET_LIST_FOR_EACH_SAFE (Entry, Next, &Service->Dns6ChildrenList) {
      Instance = NET_LIST_USER_STRUCT (Entry, DNS_INSTANCE, Link);
      
      EntryNetMap = Instance->Dns6TxTokens.Used.ForwardLink;
      while (EntryNetMap != &Instance->Dns6TxTokens.Used) {
        ItemNetMap = NET_LIST_USER_STRUCT (EntryNetMap, NET_MAP_ITEM, Link);
        Dns6TokenEntry = (DNS6_TOKEN_ENTRY *) (ItemNetMap->Key);
        if (Dns6TokenEntry->PacketToLive == 0 || (--Dns6TokenEntry->PacketToLive > 0)) {
          EntryNetMap = EntryNetMap->ForwardLink;
          continue;
        }

        //
        // Retransmit the packet if haven't reach the maxmium retry count,
        // otherwise exit the transfer.
        //
        if (++Dns6TokenEntry->Token->RetryCount < Instance->MaxRetry) {
          DnsRetransmit (Instance, (NET_BUF *) ItemNetMap->Value);
          EntryNetMap = EntryNetMap->ForwardLink;
        } else {
          //
          // Maximum retries reached, clean the Token up.
          //
          Dns6RemoveTokenEntry (&Instance->Dns6TxTokens, Dns6TokenEntry);
          Dns6TokenEntry->Token->Status = EFI_TIMEOUT;
          gBS->SignalEvent (Dns6TokenEntry->Token->Event);
          DispatchDpc ();
          
          //
          // Free the sending packet.
          //
          if (ItemNetMap->Value != NULL) {
            NetbufFree ((NET_BUF *) (ItemNetMap->Value));
          }

          EntryNetMap = Instance->Dns6TxTokens.Used.ForwardLink;
        } 
      }
    }
  } 
}

/**
  The timer ticking function for the DNS driver.

  @param  Event                 The ticking event
  @param  Context               NULL

**/
VOID
EFIAPI
DnsOnTimerUpdate (
  IN EFI_EVENT              Event,
  IN VOID                   *Context
  )
{
  LIST_ENTRY                 *Entry;
  LIST_ENTRY                 *Next;
  DNS4_CACHE                 *Item4;
  DNS6_CACHE                 *Item6;

  Item4 = NULL;
  Item6 = NULL;

  //
  // Iterate through all the DNS4 cache list.
  //
  NET_LIST_FOR_EACH_SAFE (Entry, Next, &mDriverData->Dns4CacheList) {
    Item4 = NET_LIST_USER_STRUCT (Entry, DNS4_CACHE, AllCacheLink);
    Item4->DnsCache.Timeout--;
  }
  
  Entry = mDriverData->Dns4CacheList.ForwardLink;
  while (Entry != &mDriverData->Dns4CacheList) {
    Item4 = NET_LIST_USER_STRUCT (Entry, DNS4_CACHE, AllCacheLink);
    if (Item4->DnsCache.Timeout<=0) {
      RemoveEntryList (&Item4->AllCacheLink);
      Entry = mDriverData->Dns4CacheList.ForwardLink;
    } else {
      Entry = Entry->ForwardLink;
    }
  }
  
  //
  // Iterate through all the DNS6 cache list.
  //
  NET_LIST_FOR_EACH_SAFE (Entry, Next, &mDriverData->Dns6CacheList) {
    Item6 = NET_LIST_USER_STRUCT (Entry, DNS6_CACHE, AllCacheLink);
    Item6->DnsCache.Timeout--;
  }
  
  Entry = mDriverData->Dns6CacheList.ForwardLink;
  while (Entry != &mDriverData->Dns6CacheList) {
    Item6 = NET_LIST_USER_STRUCT (Entry, DNS6_CACHE, AllCacheLink);
    if (Item6->DnsCache.Timeout<=0) {
      RemoveEntryList (&Item6->AllCacheLink);
      Entry = mDriverData->Dns6CacheList.ForwardLink;
    } else {
      Entry = Entry->ForwardLink;
    }
  }
}

