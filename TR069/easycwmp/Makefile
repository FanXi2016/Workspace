#
# Copyright (C) 2012-2014 OpenWrt.org
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk

PKG_NAME:=easycwmp
PKG_VERSION:=1.0
#PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION).tar.gz
#PKG_SOURCE_URL:=http://easycwmp.org/download/

PKG_FIXUP:=autoreconf

PKG_CONFIG_DEPENDS:= \
	CONFIG_EASYCWMP_ACS_MULTI \
	CONFIG_EASYCWMP_ACS_HDM \
	CONFIG_EASYCWMP_DEBUG \
	CONFIG_EASYCWMP_DEVEL

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)/$(PKG_NAME)-$(PKG_VERSION)

include $(INCLUDE_DIR)/package.mk

define Package/easycwmp
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=CWMP client (using libcurl)
  DEPENDS:=+libubus +libuci +libubox +libmicroxml +libjson-c +shflags +libcurl
endef

define Build/Prepare
	#mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/* $(PKG_BUILD_DIR)/	
	#sh $(TOPDIR)/deploy_custom.sh $(PKG_NAME) $(PKG_BUILD_DIR) $(TOPDIR)
endef

define Package/easycwmp/description
 An open source implementation of the client CWMP (TR-069) protocol
endef

define Package/easycwmp/config
	source "$(SOURCE)/Config.in"
endef

USE_LOCAL=$(shell ls ./src/ 2>/dev/null >/dev/null && echo 1)

TARGET_CFLAGS += \
	-D_GNU_SOURCE

TARGET_LDFLAGS += \
	-Wl,-rpath-link=$(STAGING_DIR)/usr/lib

CONFIGURE_ARGS += \
	--with-uci-include-path=$(STAGING_DIR)/usr/include \
	--with-libubox-include-path=$(STAGING_DIR)/usr/include \
	--with-libubus-include-path=$(STAGING_DIR)/usr/include


ifeq ($(CONFIG_EASYCWMP_ACS_MULTI),y)
CONFIGURE_ARGS += \
	--enable-acs=multi
endif

ifeq ($(CONFIG_EASYCWMP_ACS_HDM),y)
CONFIGURE_ARGS += \
	--enable-acs=hdm
endif

ifeq ($(CONFIG_EASYCWMP_DEBUG),y)
CONFIGURE_ARGS += \
	--enable-debug
endif

ifeq ($(CONFIG_EASYCWMP_DEVEL),y)
CONFIGURE_ARGS += \
	--enable-devel
endif

CONFIGURE_ARGS += \
	--enable-jsonc=1

define Package/easycwmp/conffiles
/etc/config/easycwmp
/usr/share/easycwmp/defaults
endef

define Package/easycwmp/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/bin/easycwmpd $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/UdpEchoService/udpechoservice $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/createfile/createfile $(1)/usr/sbin
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) $(PKG_BUILD_DIR)/ext/openwrt/config/easycwmp $(1)/etc/config
	$(INSTALL_CONF) $(PKG_BUILD_DIR)/ext/openwrt/config/modem_cfg $(1)/etc/config
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/ext/openwrt/init.d/easycwmpd $(1)/etc/init.d
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/ext/openwrt/init.d/udpechoserviced $(1)/etc/init.d
	$(INSTALL_DIR) $(1)/etc/easycwmp
ifeq ($(CONFIG_EASYCWMP_SCRIPTS_FULL),y)
	$(INSTALL_DIR) $(1)/usr/share/easycwmp
	$(CP) $(PKG_BUILD_DIR)/ext/openwrt/scripts/defaults $(1)/usr/share/easycwmp
	$(CP) $(PKG_BUILD_DIR)/ext/openwrt/scripts/functions $(1)/usr/share/easycwmp
	$(CP) $(PKG_BUILD_DIR)/ext/openwrt/scripts/oem $(1)/usr/share/easycwmp
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/ext/openwrt/scripts/oem/ping_tools $(1)/usr/share/easycwmp/oem
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/ext/openwrt/scripts/oem/upload_file_tool $(1)/usr/share/easycwmp/oem
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/ext/openwrt/scripts/oem/inform_later $(1)/usr/share/easycwmp/oem
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/ext/openwrt/scripts/easycwmp.sh $(1)/usr/sbin/easycwmp
endif
endef

$(eval $(call BuildPackage,easycwmp))
