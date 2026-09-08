variable "root_domain" {
  type        = string
  description = "Root domain name"
}

variable "cloudflare_update_dns_token_secret_name" {
  type        = string
  description = "Cloudflare update DNS token name"
}

variable "cloudflare_zone_id" {
  type        = string
  description = "Cloudflare zone ID"
}

variable "ssh_public_key_file" {
  type        = string
  description = "Path to local SSH public key file"
}

variable "ssh_username" {
  type        = string
  description = "SSH username"
}
