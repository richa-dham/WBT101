#
# This file has been automatically generated by the WICED 20719-B1 Designer.
#

APP_SRC =  ex04_connect_notify.c
APP_SRC += wiced_bt_cfg.c

C_FLAGS += -DWICED_BT_TRACE_ENABLE

# If defined, HCI traces are sent over transport/WICED HCI interface
C_FLAGS += -DHCI_TRACE_OVER_TRANSPORT

$(NAME)_COMPONENTS += gatt_utils_lib.a

########################################################################
################ DO NOT MODIFY FILE BELOW THIS LINE ####################
########################################################################

