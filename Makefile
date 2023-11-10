#
# Copyright (C) 2023 Mohy Fahim
#

include $(TOPDIR)/rules.mk

# Name, version and release number
# The name and version of your package are used to define the variable to point to the build directory of your package: $(PKG_BUILD_DIR)
PKG_NAME:=modbusgw
PKG_VERSION:=1.0
PKG_RELEASE:=1

# Source settings (i.e. where to find the source codes)
# This is a custom variable, used below
# SOURCE_DIR:=/home/mohy/work/openwrt/fahim/network/utils/modbusgw
USE_SOURCE_DIR:=/home/mohy/work/openwrt/fahim/network/utils/modbusgw
PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)

include $(INCLUDE_DIR)/package.mk
# include $(INCLUDE_DIR)/meson.mk

define Package/modbusgw
  SECTION:=net
  CATEGORY:=Network
  TITLE:=Modbusgw
  DEPENDS:=+jsoncpp +libuci +libmodbus 
endef

define Package/modbusgw/description
  Modbus tools based on libmodbus to log data fetched by a master/client or
  received by a slave/server (writing of registers). mbcollect is able to act
  as client or server (in TCP or RTU)
endef

# define Build/Configure
# 		# mkdir -p $(PKG_BUILD_DIR)
# 		# cp $(USE_SOURCE_DIR)/* $(PKG_BUILD_DIR)
# 		$(call Build/Configure/Meson)
# endef

# Package build instructions; invoke the target-specific compiler to first compile the source file, and then to link the file into the final executable
define Build/Compile
		$(TARGET_CXX) $(TARGET_CXXLAGS) -o $(PKG_BUILD_DIR)/mgd  $(PKG_BUILD_DIR)/server.cpp $(PKG_BUILD_DIR)/loguru.cpp  $(TARGET_CXXFLAGS) -luci  -ljsoncpp -lmodbus
		$(TARGET_CXX) $(TARGET_CXXLAGS) -o $(PKG_BUILD_DIR)/mgc  $(PKG_BUILD_DIR)/client.cpp  $(TARGET_CXXFLAGS) -luci  -ljsoncpp -lmodbus
endef

# Package install instructions; create a directory inside the package to hold our executable, and then copy the executable we built previously into the folder
define Package/modbusgw/install
		$(INSTALL_DIR) $(1)/usr/bin
		$(INSTALL_BIN) $(PKG_BUILD_DIR)/mgd $(1)/usr/bin
		$(INSTALL_BIN) $(PKG_BUILD_DIR)/openwrt-build/mgdc $(1)/usr/bin
		$(INSTALL_DIR) $(1)/etc/modbusgw
		# $(INSTALL_DIR) $(1)/var
		$(INSTALL_CONF) $(PKG_BUILD_DIR)/conf.json $(1)/etc/modbusgw
		$(INSTALL_CONF) $(PKG_BUILD_DIR)/registers.json $(1)/etc/modbusgw
endef

# This command is always the last, it uses the definitions and variables we give above in order to get the job done
$(eval $(call BuildPackage,modbusgw))