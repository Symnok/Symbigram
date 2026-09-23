// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
//
// TL constructor ids the client writes or tests for by hand. A constructor id is the CRC32
// of the type's schema declaration, so these are fixed forever for a given definition -
// but a single wrong digit produces a message the server discards without explanation.
// The handshake values were cross-checked against a working client (LumigramPlus); the API
// ones come from the layer-228 schema the generated table was built from.
#ifndef TLCONSTRUCTORS_H
#define TLCONSTRUCTORS_H

#include <QtGlobal>

namespace Tl
{
    const int Layer = 228;

    // Generic
    const quint32 Vector = 0x1cb5c415;
    const quint32 BoolTrue = 0x997275b5;
    const quint32 BoolFalse = 0xbc799737;

    // --- auth key generation (MTProto 2.0 handshake) ---
    const quint32 ReqPQ = 0x60469778;
    const quint32 ResPQ = 0x05162463;
    const quint32 PQInnerData = 0x83c95aec;
    const quint32 ReqDHParams = 0xd712e4be;
    const quint32 ServerDHParamsOk = 0xd0e8075c;
    const quint32 ServerDHParamsFail = 0x79cb045d;
    const quint32 ServerDHInnerData = 0xb5890dba;
    const quint32 ClientDHInnerData = 0x6643b654;
    const quint32 SetClientDHParams = 0xf5045f1f;
    const quint32 DhGenOk = 0x3bcbf734;
    const quint32 DhGenRetry = 0x46dc1fb9;
    const quint32 DhGenFail = 0xa69dae02;

    // --- service messages ---
    const quint32 RpcResult = 0xf35c6d01;
    const quint32 RpcError = 0x2144ca19;
    const quint32 MsgContainer = 0x73f1f8dc;
    const quint32 BadServerSalt = 0xedab447b;
    const quint32 BadMsgNotification = 0xa7eff811;
    const quint32 NewSessionCreated = 0x9ec20908;
    const quint32 Pong = 0x347773c5;
    const quint32 PingDelayDisconnect = 0xf3427b8c;
    const quint32 MsgsAck = 0x62d6b459;
    const quint32 GzipPacked = 0x3072cfa1;
    const quint32 MsgDetailedInfo = 0x276d3ec6;
    const quint32 MsgNewDetailedInfo = 0x809db6df;
    const quint32 MsgsStateReq = 0xda69fb52;
    const quint32 MsgsStateInfo = 0x04deb57d;
    const quint32 MsgsAllInfo = 0x8cc0d131;

    // --- API layer 228 ---
    const quint32 InvokeWithLayer = 0xda9b0d0d;
    const quint32 InitConnection = 0xc1cd5ea9;
    const quint32 HelpGetNearestDc = 0x1fb33026;
    const quint32 NearestDc = 0x8e1a1775;
    const quint32 HelpGetConfig = 0xc4f9186b;
    const quint32 Config = 0xcc1a241e;
    const quint32 DcOption = 0x18b7a10d;

    // QR login
    const quint32 AuthExportLoginToken = 0xb7e085fe;
    const quint32 AuthImportLoginToken = 0x95ac5ce4;
    const quint32 AuthLoginToken = 0x629f1980;
    const quint32 AuthLoginTokenMigrateTo = 0x068e9916;
    const quint32 AuthLoginTokenSuccess = 0x390d5c5e;
    const quint32 UpdateLoginToken = 0x564fe691;
    const quint32 AuthAuthorization = 0x2ea2c0d4;
    const quint32 AuthLogOut = 0x3e72ba19;
    const quint32 AuthLoggedOut = 0xc3a2835f;

    // two-step verification (SRP)
    const quint32 AccountGetPassword = 0x548a30f5;
    const quint32 AccountPassword = 0x957b50fb;
    const quint32 PasswordKdfAlgoSha256Pbkdf2 = 0x3a912d4a;
    const quint32 InputCheckPasswordSrp = 0xd27ff082;
    const quint32 AuthCheckPassword = 0xd18b4d16;

    // peers
    const quint32 InputPeerEmpty = 0x7f3b18ea;
    const quint32 InputPeerSelf = 0x7da07ec9;
    const quint32 InputPeerUser = 0xdde8a54c;
    const quint32 InputPeerChat = 0x35a95cb9;
    const quint32 InputPeerChannel = 0x27bcbbfc;
    const quint32 InputUser = 0xf21158c6;
    const quint32 InputUserSelf = 0xf7c1b13f;
    const quint32 InputChannel = 0xf35aec28;
    const quint32 PeerUser = 0x59511722;
    const quint32 PeerChat = 0x36c6019a;
    const quint32 PeerChannel = 0xa2a5371e;
    const quint32 User = 0xb1b8cc83;
    const quint32 UserEmpty = 0xd3bc4b7a;
    const quint32 Chat = 0x41cbf256;
    const quint32 ChatEmpty = 0x29562865;
    const quint32 ChatForbidden = 0x6592a1a7;
    const quint32 Channel = 0xd49f34c6;
    const quint32 ChannelForbidden = 0x17d493d5;
    const quint32 UserProfilePhoto = 0x82d1f706;
    const quint32 ChatPhoto = 0x1c6e1c11;
    const quint32 UserStatusEmpty = 0x09d05049;
    const quint32 UserStatusOnline = 0xedb93949;
    const quint32 UserStatusOffline = 0x008c703f;
    const quint32 UserStatusRecently = 0x7b197dc8;
    const quint32 UserStatusLastWeek = 0x541a1d1a;
    const quint32 UserStatusLastMonth = 0x65899777;
    const quint32 UsersGetUsers = 0x0d91a548;
    const quint32 ContactsResolveUsername = 0x725afbbc;
    const quint32 ContactsResolvePhone = 0x8af94344;
    const quint32 ContactsResolvedPeer = 0x7f077ad9;
    const quint32 ContactsSearch = 0x05f58d0f;
    const quint32 ContactsFound = 0xb3134d9d;
    const quint32 ContactsGetContacts = 0x5dd69e12;
    const quint32 ContactsContacts = 0xeae87e42;

    // dialogs and messages
    const quint32 MessagesGetDialogs = 0xa0f4cb4f;
    const quint32 MessagesDialogs = 0x15ba6c40;
    const quint32 MessagesDialogsSlice = 0x71e094f3;
    const quint32 MessagesDialogsNotModified = 0xf0e3e596;
    const quint32 Dialog = 0xfc89f7f3;
    const quint32 DialogFolder = 0x71bd134c;
    const quint32 PeerNotifySettings = 0x99622c0c;
    const quint32 MessagesGetHistory = 0x4423e6c5;
    const quint32 MessagesMessages = 0x1d73e7ea;
    const quint32 MessagesMessagesSlice = 0x5f206716;
    const quint32 MessagesChannelMessages = 0xc776ba4e;
    const quint32 Message = 0x7600b9d3;
    const quint32 MessageEmpty = 0x90a6ca84;
    const quint32 MessageService = 0x7a800e0a;
    const quint32 MessagesSendMessage = 0xfef48f62;
    const quint32 InputReplyToMessage = 0x3bd4b7c2;
    const quint32 MessagesReadHistory = 0x0e306d3a;
    const quint32 ChannelsReadHistory = 0xcc104937;
    const quint32 MessagesAffectedMessages = 0x84d19185;
    const quint32 MessagesDeleteMessages = 0xe58e95d2;
    const quint32 ChannelsDeleteMessages = 0x84c1fd4e;
    const quint32 MessagesDeleteHistory = 0xb08f922a;
    const quint32 MessagesDeleteChatUser = 0xa2185cab;
    const quint32 ChannelsLeaveChannel = 0xf836aa95;
    const quint32 FoldersEditPeerFolders = 0x6847d0ab;
    const quint32 InputFolderPeer = 0xfbd2c296;
    const quint32 MessagesUpdateDialogFilter = 0x1ad4a04a;
    const quint32 MessagesSetTyping = 0x58943ee2;
    const quint32 SendMessageTypingAction = 0x16bf744e;
    const quint32 SendMessageCancelAction = 0xfd5ec8f5;
    const quint32 AccountUpdateStatus = 0x6628562c;
    const quint32 AccountUpdateNotifySettings = 0x84be5b93;
    const quint32 InputNotifyPeer = 0xb8bc5b0c;
    const quint32 InputPeerNotifySettings = 0xcacb6ae2;
    const quint32 MessagesForwardMessages = 0x13704a7c;
    const quint32 MessageFwdHeader = 0x4e4df4bb;
    const quint32 MessageReplyHeader = 0x1b97dd66;

    // service message actions
    const quint32 MessageActionChatCreate = 0xbd47cbad;
    const quint32 MessageActionChatAddUser = 0x15cefd00;
    const quint32 MessageActionChatDeleteUser = 0xa43f30cc;
    const quint32 MessageActionChatJoinedByLink = 0x031224c3;
    const quint32 MessageActionPinMessage = 0x94bd38ed;
    const quint32 MessageActionContactSignUp = 0xf3f25f76;
    const quint32 MessageActionHistoryClear = 0x9fbab604;
    const quint32 MessageActionChannelCreate = 0x95d2ac92;

    // media
    const quint32 MessageMediaPhoto = 0xe216eb63;
    const quint32 MessageMediaDocument = 0x52d8ccd9;
    const quint32 MessageMediaGeo = 0x56e0d474;
    const quint32 MessageMediaGeoLive = 0xb940c666;
    const quint32 MessageMediaContact = 0x70322949;
    const quint32 MessageMediaWebPage = 0xddf10c3b;
    const quint32 MessageMediaVenue = 0x2ec0533f;
    const quint32 MessageMediaPoll = 0x773f4e66;
    const quint32 MessageMediaDice = 0x08cbec07;
    const quint32 MessageMediaGame = 0xfdb19008;
    const quint32 MessageMediaInvoice = 0xf6a548d3;
    const quint32 MessageMediaStory = 0x68cb6283;
    const quint32 MessageMediaUnsupported = 0x9f84f49e;
    const quint32 Photo = 0xfb197a65;
    const quint32 PhotoEmpty = 0x2331b22d;
    const quint32 Document = 0x8fd4c4d8;
    const quint32 DocumentEmpty = 0x36f8c871;
    const quint32 GeoPoint = 0xb2a2f663;
    const quint32 DocumentAttributeImageSize = 0x6c37c15c;
    const quint32 DocumentAttributeAnimated = 0x11b58939;
    const quint32 DocumentAttributeSticker = 0x6319d612;
    const quint32 DocumentAttributeVideo = 0x43c57c48;
    const quint32 DocumentAttributeAudio = 0x9852f9c6;
    const quint32 DocumentAttributeFilename = 0x15590068;
    const quint32 DocumentAttributeCustomEmoji = 0xfd149899;
    const int DocumentAttributeAudioVoiceFlag = 1 << 10;
    const int DocumentAttributeVideoRoundFlag = 1 << 0;

    // files
    const quint32 UploadGetFile = 0xbe5335be;
    const quint32 UploadFile = 0x096a18d5;
    const quint32 UploadFileCdnRedirect = 0xf18cda44;
    const quint32 InputPhotoFileLocation = 0x40181ffe;
    const quint32 InputDocumentFileLocation = 0xbad07584;
    const quint32 InputPeerPhotoFileLocation = 0x37257e99;
    const quint32 PhotoSize = 0x75c78e60;
    const quint32 PhotoCachedSize = 0x021e1ad6;
    const quint32 PhotoStrippedSize = 0xe0b0bc2e;
    const quint32 PhotoSizeProgressive = 0xfa3efb95;
    const quint32 UploadSaveFilePart = 0xb304a621;
    const quint32 UploadSaveBigFilePart = 0xde7b673d;
    const quint32 InputFile = 0xf52ff27f;
    const quint32 InputFileBig = 0xfa4f0bb5;
    const quint32 InputMediaUploadedPhoto = 0x7d8375da;
    const quint32 InputMediaUploadedDocument = 0x037c9330;
    const quint32 MessagesSendMedia = 0x0330e77f;
    const quint32 AuthExportAuthorization = 0xe5bfffcd;
    const quint32 AuthExportedAuthorization = 0xb434e2b8;
    const quint32 AuthImportAuthorization = 0xa57a7dad;

    // updates
    const quint32 UpdatesGetState = 0xedd4882a;
    const quint32 UpdatesState = 0xa56c2a3e;
    const quint32 UpdatesGetDifference = 0x19c2f763;
    const quint32 UpdatesDifference = 0x00f49ca0;
    const quint32 UpdatesDifferenceEmpty = 0x5d75a138;
    const quint32 UpdatesDifferenceSlice = 0xa8fb1981;
    const quint32 UpdatesDifferenceTooLong = 0x4afe8f6d;
    const quint32 UpdatesTooLong = 0xe317af7e;
    const quint32 UpdateShortMessage = 0x313bc7f8;
    const quint32 UpdateShortChatMessage = 0x4d6deea5;
    const quint32 UpdateShort = 0x78d4dec1;
    const quint32 UpdatesCombined = 0x725b04c3;
    const quint32 Updates = 0x74ae4240;
    const quint32 UpdateShortSentMessage = 0x9015e101;
    const quint32 UpdateNewMessage = 0x1f2b0afd;
    const quint32 UpdateNewChannelMessage = 0x62ba04d9;
    const quint32 UpdateMessageID = 0x4e90bfd6;
    const quint32 UpdateDeleteMessages = 0xa20db0e5;
    const quint32 UpdateEditMessage = 0xe40370a3;
    const quint32 UpdateEditChannelMessage = 0x1b3f4df7;
    const quint32 UpdateUserTyping = 0x2a17bf5c;
    const quint32 UpdateChatUserTyping = 0x83487af0;
    const quint32 UpdateChannelUserTyping = 0x8c88c923;
    const quint32 UpdateUserStatus = 0xe5bdf8de;
    const quint32 UpdateUserName = 0xa7848924;
    const quint32 UpdateUser = 0x20529438;
    const quint32 UpdateReadHistoryInbox = 0x9e84bc99;
    const quint32 UpdateReadHistoryOutbox = 0x2f2f21bf;
    const quint32 UpdateReadChannelInbox = 0x922e6e10;
    const quint32 UpdateReadChannelOutbox = 0xb75f99a9;
    const quint32 UpdateNotifySettings = 0xbec268ef;
    const quint32 UpdateChannelTooLong = 0x108d941f;
    const quint32 MessagesGetDialogFilters = 0xefd48c89;
    const quint32 MessagesDialogFilters = 0x2ad93719;
    const quint32 DialogFilter = 0xaa472651;
    const quint32 DialogFilterChatlist = 0x96537bd7;
    const quint32 DialogFilterDefault = 0x363293ae;
    const quint32 UpdateDialogFilter = 0x26ffde7d;
    const quint32 UpdateDialogFilters = 0x3504914f;
    const quint32 UpdateDialogFilterOrder = 0xa5d72105;
    const quint32 UpdateFolderPeers = 0x19360dc0;
    const quint32 FolderPeer = 0xe9baa668;
    const quint32 InputPeerUserFromMessage = 0xa87b0a1c;
    const quint32 InputPeerChannelFromMessage = 0xbd2a0840;
    const quint32 TextWithEntities = 0x751f3146;
    const quint32 UpdateDialogUnreadMark = 0xb658f23e;

    // --- secret chats (end-to-end) ---
    const int SecretLayer = 73;                    // the secret-chat layer we advertise
    const quint32 MessagesGetDhConfig = 0x26cf8950;
    const quint32 MessagesDhConfig = 0x2c221edd;
    const quint32 MessagesDhConfigNotModified = 0xc0e24635;
    const quint32 MessagesRequestEncryption = 0xf64daf43;
    const quint32 MessagesAcceptEncryption = 0x3dbc0415;
    const quint32 MessagesDiscardEncryption = 0xf393aea0;
    const quint32 MessagesSetEncryptedTyping = 0x791451ed;
    const quint32 MessagesReadEncryptedHistory = 0x7f4b690a;
    const quint32 MessagesSendEncrypted = 0x44fa7a15;
    const quint32 MessagesSendEncryptedFile = 0x5559481d;
    const quint32 MessagesSendEncryptedService = 0x32d439a4;
    const quint32 MessagesSentEncryptedMessage = 0x560f8935;
    const quint32 MessagesSentEncryptedFile = 0x9493ff32;
    const quint32 EncryptedChatEmpty = 0xab7ec0a0;
    const quint32 EncryptedChatWaiting = 0x66b25953;
    const quint32 EncryptedChatRequested = 0x48f1d94c;
    const quint32 EncryptedChat = 0x61f0d4c7;
    const quint32 EncryptedChatDiscarded = 0x1e1c7c45;
    const quint32 InputEncryptedChat = 0xf141b5e1;
    const quint32 EncryptedMessage = 0xed18c118;
    const quint32 EncryptedMessageService = 0x23734b06;
    const quint32 UpdateNewEncryptedMessage = 0x12bcbd9a;
    const quint32 UpdateEncryption = 0xb4a2e88d;
    const quint32 UpdateEncryptedChatTyping = 0x1710f156;
    const quint32 UpdateEncryptedMessagesRead = 0x38fe25b7;
    // hand-used secret-schema constructors
    const quint32 DecryptedMessage73 = 0x91cc4674;         // decryptedMessage (layer 73)
    const quint32 DecryptedMessageService = 0x73164160;
    const quint32 DecryptedMessageLayer = 0x1be31789;
    const quint32 DecryptedMessageActionNotifyLayer = 0xf3048883;
    const quint32 DecryptedMessageActionSetMessageTTL = 0xa1733aec;
    const quint32 DecryptedMessageActionReadMessages = 0x0c4f40be;
    const quint32 DecryptedMessageActionDeleteMessages = 0x65614304;
    const quint32 DecryptedMessageActionFlushHistory = 0x6719e45c;
    const quint32 DecryptedMessageActionTyping = 0xccb27641;
    const quint32 DecryptedMessageMediaEmpty = 0x089f5c4a;

    // --- phone-code login (harness only; the app stays QR-only) ---
    const quint32 AuthSendCode = 0xa677244f;
    const quint32 AuthSignIn = 0x8d52a951;
    const quint32 AuthResendCode = 0xcae47523;
    const quint32 AuthSentCode = 0x5e002502;
    const quint32 AuthSentCodeSuccess = 0x2390fe44;
    const quint32 CodeSettings = 0xad253d78;
}

#endif // TLCONSTRUCTORS_H
